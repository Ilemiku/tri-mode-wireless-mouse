// 默认只上传检查；--upgrade才发布实物升级命令。下载/重启期间不操作ZE。
import { readFile, writeFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';
import mqtt from 'mqtt';
const file = process.argv.find((a) => a.endsWith('.bin'));
if (!file)
  throw new Error('Usage: node tools/verify-ota.mjs app.bin [--upgrade] [--expect-rollback]');
const expectRollback = process.argv.includes('--expect-rollback');
if (expectRollback && !process.argv.includes('--upgrade'))
  throw new Error('--expect-rollback requires --upgrade');
const token = (await readFile(new URL('../.local/token', import.meta.url), 'utf8')).trim();
const body = await readFile(file),
  response = await fetch('http://127.0.0.1:8088/api/firmware', {
    method: 'POST',
    headers: { Authorization: `Bearer ${token}` },
    body,
  });
const pkg = await response.json();
if (!response.ok) throw new Error(JSON.stringify(pkg));
console.log('Checked package:', JSON.stringify(pkg));
if (!process.argv.includes('--upgrade')) process.exit(0);
const device = process.env.MOUSE_DEVICE || '28848560da8c',
  id = randomUUID(),
  records = [];
const client = mqtt.connect('mqtt://127.0.0.1:18830', {
  username: 'mouse',
  password: token,
  protocolVersion: 4,
  reconnectPeriod: 0,
});
let timer;
try {
  await once(client, 'connect');
  await client.subscribeAsync(
    [`mouse/${device}/ota/status`, `mouse/${device}/result`, `mouse/${device}/state`],
    { qos: 1 },
  );
  const done = new Promise((resolve, reject) => {
    timer = setTimeout(
      () =>
        reject(new Error('OTA result unknown after 180 seconds; inspect live state before retry')),
      180000,
    );
    client.on('message', (topic, bytes) => {
      let msg;
      try {
        msg = JSON.parse(bytes.toString());
      } catch {
        return;
      }
      if (topic.endsWith('/state')) return;
      if (msg.id !== id) return;
      records.push({ topic, ...msg });
      console.log(JSON.stringify(msg));
      if (topic.endsWith('/result') && msg.status === 'rejected')
        reject(new Error('OTA rejected: ' + msg.detail));
      if (expectRollback && msg.phase === 'rolled_back' && msg.running_version !== pkg.version) {
        resolve();
        return;
      }
      if (msg.phase === 'confirmed' && msg.running_version === pkg.version) {
        if (expectRollback) reject(new Error('Fault-injection image unexpectedly confirmed'));
        else resolve();
      }
      if (['failed', 'rolled_back', 'interrupted'].includes(msg.phase))
        reject(new Error('OTA ended: ' + msg.phase + ' / ' + msg.detail));
    });
  });
  // 先注册接收者再发送，避免设备快速应答早于订阅。
  client.publish(`mouse/${device}/command`, JSON.stringify({ id, op: 'ota_start', ...pkg }), {
    qos: 1,
    retain: false,
  });
  await done;
  console.log(
    expectRollback
      ? 'Real fault-injection OTA + automatic rollback verified'
      : 'Real HTTPS OTA + reboot + self-test confirmed',
  );
} finally {
  clearTimeout(timer);
  await writeFile(
    new URL(
      expectRollback ? '../.local/ota-rollback-run.json' : '../.local/ota-last-run.json',
      import.meta.url,
    ),
    JSON.stringify({ id, package: pkg, records }, null, 2),
  );
  await client.endAsync(true);
}
