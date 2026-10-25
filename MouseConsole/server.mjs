import { Aedes } from 'aedes';
import { createServer as tcpServer } from 'node:net';
import { createServer } from 'node:http';
import { createServer as httpsServer } from 'node:https';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { WebSocketServer, createWebSocketStream } from 'ws';
import { fileURLToPath } from 'node:url';
import { authorized, createFirmwareStore, uploadFirmware } from './firmware.mjs';

/* 同一进程提供三个服务，职责分开：
 * HTTP/WebSocket：控制台与本机升级包上传；TCP：MQTT Broker；HTTPS：设备下载固件。
 * Broker只转发消息，不能替ESP32产生“硬件执行成功”或“升级已确认”的回执。
 */
export async function startConsole({
  webPort = 8088,
  mqttPort = 18830,
  host = '0.0.0.0',
  token,
  ota = null,
} = {}) {
  const root = new URL('./', import.meta.url);
  if (!token) {
    await mkdir(new URL('.local/', root), { recursive: true });
    const path = new URL('.local/token', root);
    try {
      token = (await readFile(path, 'utf8')).trim();
    } catch {
      token = randomBytes(24).toString('hex');
      await writeFile(path, token, { mode: 0o600, flag: 'wx' });
    }
  }
  const broker = await Aedes.createBroker({ drainTimeout: 10000 });
  const firmware = ota
    ? await createFirmwareStore(ota.directory, ota.host, ota.port || 8089)
    : null;
  broker.authenticate = (client, user, password, done) =>
    done(null, user === 'mouse' && password?.toString() === token);
  broker.authorizePublish = (client, packet, done) => {
    const ok = /^mouse\/[a-f0-9]{12}\/(command|result|state|availability|ota\/status)$/.test(
      packet.topic,
    );
    if (packet.topic.endsWith('/command') && packet.retain)
      return done(new Error('commands must not be retained'));
    done(ok ? null : new Error('invalid topic'));
  };
  broker.authorizeSubscribe = (client, sub, done) =>
    done(
      sub.topic === 'mouse/+/state' ||
        sub.topic === 'mouse/+/result' ||
        sub.topic === 'mouse/+/availability' ||
        sub.topic === 'mouse/+/ota/status' ||
        /^mouse\/[a-f0-9]{12}\/(command|result|state|availability|ota\/status)$/.test(sub.topic)
        ? null
        : new Error('invalid subscription'),
      sub,
    );
  broker.on('clientError', (_, error) => console.error('MQTT client:', error.message));
  const tcp = tcpServer(broker.handle);
  const web = createServer(async (req, res) => {
    try {
      const path = new URL(req.url, 'http://localhost').pathname;
      if (path === '/api/firmware') {
        if (!firmware) {
          res.writeHead(503).end('{"error":"OTA证书尚未配置"}');
          return;
        }
        await uploadFirmware(req, res, firmware, token);
        return;
      }
      if (req.method !== 'GET') {
        res.writeHead(405).end();
        return;
      }
      if (path === '/health') {
        res.setHeader('Content-Type', 'application/json');
        res.end('{"ok":true}');
        return;
      }
      const file =
        path === '/'
          ? 'public/index.html'
          : path === '/app.js'
            ? 'public/app.js'
            : path === '/style.css'
              ? 'public/style.css'
              : path === '/mqtt.js'
                ? 'node_modules/mqtt/dist/mqtt.min.js'
                : null;
      if (!file) {
        res.writeHead(404).end();
        return;
      }
      const content = await readFile(new URL(file, root));
      res.setHeader(
        'Content-Type',
        file.endsWith('.html')
          ? 'text/html; charset=utf-8'
          : file.endsWith('.css')
            ? 'text/css'
            : 'text/javascript',
      );
      res.setHeader('Cache-Control', 'no-store');
      res.end(content);
    } catch {
      res.writeHead(500).end('server error');
    }
  });
  const sockets = new WebSocketServer({ server: web, path: '/mqtt', maxPayload: 4096 });
  sockets.on('connection', (ws) => broker.handle(createWebSocketStream(ws)));
  let downloads;
  if (ota) {
    downloads = httpsServer({ key: ota.key, cert: ota.cert }, async (req, res) => {
      try {
        if (!authorized(req.headers.authorization, token)) {
          res.writeHead(401).end();
          return;
        }
        if (req.method !== 'GET') {
          res.writeHead(405).end();
          return;
        }
        const bytes = await firmware.read(new URL(req.url, 'https://localhost').pathname);
        if (!bytes) {
          res.writeHead(404).end();
          return;
        }
        res.writeHead(200, {
          'Content-Type': 'application/octet-stream',
          'Content-Length': bytes.length,
          'Cache-Control': 'no-store',
        });
        res.end(bytes);
      } catch {
        if (!res.headersSent) res.writeHead(500);
        res.end();
      }
    });
    await new Promise((resolve, reject) => {
      downloads.once('error', reject);
      downloads.listen(ota.port || 8089, host, resolve);
    });
  }
  await new Promise((resolve, reject) => {
    tcp.once('error', reject);
    tcp.listen(mqttPort, host, resolve);
  });
  try {
    await new Promise((resolve, reject) => {
      web.once('error', reject);
      web.listen(webPort, host, resolve);
    });
  } catch (e) {
    tcp.close();
    await new Promise((resolve) => broker.close(resolve));
    throw e;
  }
  return {
    token,
    webPort: web.address().port,
    mqttPort: tcp.address().port,
    async close() {
      for (const ws of sockets.clients) ws.terminate();
      if (downloads) {
        downloads.closeAllConnections();
        await new Promise((resolve) => downloads.close(resolve));
      }
      await new Promise((resolve) => broker.close(resolve));
      await Promise.all([
        new Promise((resolve) => tcp.close(resolve)),
        new Promise((resolve) => web.close(resolve)),
      ]);
    },
  };
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  let ota = null;
  try {
    ota = {
      host: (await readFile(new URL('.local/ota-host.txt', import.meta.url), 'utf8')).trim(),
      key: await readFile(new URL('.local/ota-server-key.pem', import.meta.url)),
      cert: await readFile(new URL('.local/ota-server.pem', import.meta.url)),
      directory: fileURLToPath(new URL('.local/firmware/', import.meta.url)),
    };
  } catch (e) {
    if (e.code !== 'ENOENT') throw e;
  }
  const app = await startConsole({
    webPort: Number(process.env.WEB_PORT || 8088),
    mqttPort: Number(process.env.MQTT_PORT || 18830),
    ota,
  });
  if (ota)
    console.log(`OTA HTTPS文件服务: https://${ota.host}:8089 (设备信任项目CA，不关闭证书校验)`);
  console.log(
    `Mouse Console: http://localhost:${app.webPort}\nMQTT TCP: ${app.mqttPort}\n配网与网页连接令牌: ${app.token}\n仅用于可信局域网；退出按Ctrl+C。`,
  );
  for (const signal of ['SIGINT', 'SIGTERM'])
    process.once(signal, async () => {
      await app.close();
      process.exit(0);
    });
}
