import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createServer } from 'node:http';
import { inspectFirmware, authorized, createFirmwareStore, uploadFirmware } from '../firmware.mjs';

function image() {
  const b = Buffer.alloc(400);
  b[0] = 0xe9;
  b.writeUInt16LE(9, 12);
  b.writeUInt32LE(0xabcd5432, 32);
  b.write('0.2.1', 48);
  b.write('Esp32Mouse', 80);
  return b;
}
test('OTA仅接受对应芯片、项目、大小的应用镜像', () => {
  const b = image();
  assert.equal(inspectFirmware(b).version, '0.2.1');
  const wrongChip = Buffer.from(b);
  wrongChip.writeUInt16LE(0, 12);
  assert.throws(() => inspectFirmware(wrongChip));
  const wrongProject = Buffer.from(b);
  wrongProject.write('NotOurApp!', 80);
  assert.throws(() => inspectFirmware(wrongProject));
  assert.throws(() => inspectFirmware(Buffer.alloc(20)));
  assert.throws(() => inspectFirmware(Buffer.alloc(2 * 1024 * 1024 + 1)));
  assert.equal(authorized('Bearer secret', 'secret'), true);
  assert.equal(authorized('Bearer wrong', 'secret'), false);
});
test('上传API需要令牌，拒绝坏镜像和超大包，正确返回镜像版本', async () => {
  const store = await createFirmwareStore(
    await mkdtemp(join(tmpdir(), 'mouse-ota-api-test-')),
    '192.168.10.82',
  );
  const server = createServer((req, res) => uploadFirmware(req, res, store, 'test-token'));
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const url = `http://127.0.0.1:${server.address().port}`;
  const headers = { Authorization: 'Bearer test-token' };
  try {
    assert.equal((await fetch(url, { method: 'POST', body: image() })).status, 401);
    assert.equal(
      (await fetch(url, { method: 'POST', headers, body: Buffer.alloc(300) })).status,
      400,
    );
    assert.equal(
      (await fetch(url, { method: 'POST', headers, body: Buffer.alloc(2 * 1024 * 1024 + 1) }))
        .status,
      413,
    );
    const result = await fetch(url, { method: 'POST', headers, body: image() });
    assert.equal(result.status, 201);
    assert.equal((await result.json()).version, '0.2.1');
    assert.equal((await (await fetch(url, { headers })).json()).length, 1);
  } finally {
    server.closeAllConnections();
    await new Promise((resolve) => server.close(resolve));
  }
});
test('升级包按SHA256不可变寻址，不接受目录穿越', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'mouse-ota-test-'));
  const store = await createFirmwareStore(dir, '192.168.10.82');
  const b = image(),
    info = await store.add(b);
  assert.match(info.url, /^https:\/\/192\.168\.10\.82:8089\/firmware\/[a-f0-9]{64}\.bin$/);
  assert.deepEqual(await store.read(`/firmware/${info.sha256}.bin`), b);
  assert.equal(await store.read('/firmware/../../token'), null);
  assert.equal((await store.list()).length, 1);
  assert.deepEqual(await store.add(b), info);
});
