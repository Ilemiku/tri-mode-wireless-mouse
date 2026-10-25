// 升级包只是app.bin，不接受bootloader、分区表或整片Flash备份。
import { createHash, timingSafeEqual } from 'node:crypto';
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

export const MAX_FIRMWARE_SIZE = 2 * 1024 * 1024;
export function authorized(header, token) {
  const expected = Buffer.from(`Bearer ${token}`),
    actual = Buffer.from(header || '');
  return expected.length === actual.length && timingSafeEqual(expected, actual);
}
export function inspectFirmware(bytes) {
  if (bytes.length < 288 || bytes.length > MAX_FIRMWARE_SIZE)
    throw new Error('升级包必须为288字节至2MiB的ESP32应用镜像');
  if (bytes[0] !== 0xe9 || bytes.readUInt16LE(12) !== 9 || bytes.readUInt32LE(32) !== 0xabcd5432)
    throw new Error('不是ESP32-S3应用镜像；不要选择bootloader或合并镜像');
  const cstr = (offset, size) => {
    const data = bytes.subarray(offset, offset + size),
      end = data.indexOf(0);
    if (end < 0) throw new Error('镜像描述字段未终止');
    return data.subarray(0, end).toString('utf8');
  };
  const version = cstr(48, 32),
    project = cstr(80, 32);
  if (project !== 'Esp32Mouse' || !/^[a-zA-Z0-9._-]{1,31}$/.test(version))
    throw new Error('工程名或版本不符合鼠标固件要求');
  return {
    version,
    project,
    size: bytes.length,
    sha256: createHash('sha256').update(bytes).digest('hex'),
  };
}
export async function createFirmwareStore(directory, host, port = 8089) {
  if (!/^[a-zA-Z0-9.-]+$/.test(host)) throw new Error('OTA发布地址无效');
  await mkdir(directory, { recursive: true });
  const manifest = (info) => ({
    ...info,
    url: `https://${host}:${port}/firmware/${info.sha256}.bin`,
  });
  return {
    async add(bytes) {
      const info = inspectFirmware(bytes);
      try {
        await writeFile(join(directory, `${info.sha256}.bin`), bytes, { flag: 'wx' });
      } catch (e) {
        if (e.code !== 'EEXIST') throw e;
      }
      return manifest(info);
    },
    async list() {
      const items = [];
      for (const file of await readdir(directory)) {
        if (!/^[a-f0-9]{64}\.bin$/.test(file)) continue;
        try {
          const bytes = await readFile(join(directory, file));
          const info = inspectFirmware(bytes);
          if (file === `${info.sha256}.bin`) items.push(manifest(info));
        } catch {
          /* 不把损坏包列为可升级版本。 */
        }
      }
      return items;
    },
    async read(path) {
      if (!/^\/firmware\/[a-f0-9]{64}\.bin$/.test(path)) return null;
      try {
        return await readFile(join(directory, path.slice('/firmware/'.length)));
      } catch (e) {
        if (e.code === 'ENOENT') return null;
        throw e;
      }
    },
  };
}
export async function uploadFirmware(req, res, store, token) {
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.setHeader('Cache-Control', 'no-store');
  if (!authorized(req.headers.authorization, token)) {
    res.writeHead(401).end(JSON.stringify({ error: '连接令牌无效' }));
    return;
  }
  if (req.method === 'GET') {
    res.end(JSON.stringify(await store.list()));
    return;
  }
  if (req.method !== 'POST') {
    res.writeHead(405).end();
    return;
  }
  const chunks = [];
  let length = 0;
  try {
    for await (const chunk of req) {
      length += chunk.length;
      if (length > MAX_FIRMWARE_SIZE) {
        res.writeHead(413).end(JSON.stringify({ error: '升级包超过2MiB' }));
        return;
      }
      chunks.push(chunk);
    }
    const result = await store.add(Buffer.concat(chunks));
    res.writeHead(201).end(JSON.stringify(result));
  } catch (e) {
    res.writeHead(400).end(JSON.stringify({ error: e.message }));
  }
}
