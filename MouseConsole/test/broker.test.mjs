import test from 'node:test';
import assert from 'node:assert/strict';
import mqtt from 'mqtt';
import { once } from 'node:events';
import { startConsole } from '../server.mjs';

test('真实MQTT TCP和WebSocket收发：PUBACK不伪造设备执行回执', { timeout: 5000 }, async () => {
  const app = await startConsole({
    host: '127.0.0.1',
    mqttPort: 0,
    webPort: 0,
    token: 'test-only',
  });
  const device = mqtt.connect(`mqtt://127.0.0.1:${app.mqttPort}`, {
    username: 'mouse',
    password: app.token,
    protocolVersion: 4,
    reconnectPeriod: 0,
  });
  const web = mqtt.connect(`ws://127.0.0.1:${app.webPort}/mqtt`, {
    username: 'mouse',
    password: app.token,
    protocolVersion: 4,
    reconnectPeriod: 0,
  });
  try {
    await Promise.all([once(device, 'connect'), once(web, 'connect')]);
    await device.subscribeAsync('mouse/aabbccddeeff/command', { qos: 1 });
    await web.subscribeAsync('mouse/+/result', { qos: 1 });
    const received = once(device, 'message'),
      command = { id: 'case1', op: 'set_dpi', dpi: 1600 };
    await web.publishAsync('mouse/aabbccddeeff/command', JSON.stringify(command), {
      qos: 1,
      retain: false,
    });
    const [topic, body] = await received;
    assert.equal(topic, 'mouse/aabbccddeeff/command');
    assert.deepEqual(JSON.parse(body), command);
    const result = once(web, 'message');
    await device.publishAsync(
      'mouse/aabbccddeeff/result',
      JSON.stringify({ id: 'case1', status: 'applied', dpi: 1600 }),
      { qos: 1 },
    );
    assert.equal(JSON.parse((await result)[1]).status, 'applied');
    const response = await fetch(`http://127.0.0.1:${app.webPort}/`);
    assert.equal(response.status, 200);
    assert.match(await response.text(), /传感器实际DPI/);
  } finally {
    await Promise.all([device.endAsync(true), web.endAsync(true)]);
    await app.close();
  }
});

test('错误令牌不能登录Broker', { timeout: 5000 }, async () => {
  const app = await startConsole({
    host: '127.0.0.1',
    mqttPort: 0,
    webPort: 0,
    token: 'test-only',
  });
  const rejected = mqtt.connect(`mqtt://127.0.0.1:${app.mqttPort}`, {
    username: 'mouse',
    password: 'wrong',
    protocolVersion: 4,
    reconnectPeriod: 0,
  });
  try {
    const [error] = await once(rejected, 'error');
    assert.equal(error.code, 5);
  } finally {
    await rejected.endAsync(true);
    await app.close();
  }
});

test('拒绝保留命令，避免重连时误执行旧动作', { timeout: 5000 }, async () => {
  const app = await startConsole({
    host: '127.0.0.1',
    mqttPort: 0,
    webPort: 0,
    token: 'test-only',
  });
  const publisher = mqtt.connect(`mqtt://127.0.0.1:${app.mqttPort}`, {
    username: 'mouse',
    password: app.token,
    protocolVersion: 4,
    reconnectPeriod: 0,
  });
  try {
    await once(publisher, 'connect');
    const disconnected = once(publisher, 'close');
    publisher.publish('mouse/aabbccddeeff/command', '{"op":"set_dpi","dpi":1600}', {
      qos: 1,
      retain: true,
    });
    await disconnected;
  } finally {
    await publisher.endAsync(true);
    await app.close();
  }
});

test('OTA状态可保留重放，但不会把100%自动改成confirmed', { timeout: 5000 }, async () => {
  const app = await startConsole({
    host: '127.0.0.1',
    mqttPort: 0,
    webPort: 0,
    token: 'test-only',
  });
  const device = mqtt.connect(`mqtt://127.0.0.1:${app.mqttPort}`, {
    username: 'mouse',
    password: app.token,
    reconnectPeriod: 0,
  });
  const web = mqtt.connect(`ws://127.0.0.1:${app.webPort}/mqtt`, {
    username: 'mouse',
    password: app.token,
    reconnectPeriod: 0,
  });
  try {
    await Promise.all([once(device, 'connect'), once(web, 'connect')]);
    const status = { id: 'ota1', phase: 'self_testing', percent: 100, running_version: '0.2.1' };
    await device.publishAsync('mouse/aabbccddeeff/ota/status', JSON.stringify(status), {
      qos: 1,
      retain: true,
    });
    const replay = once(web, 'message');
    await web.subscribeAsync('mouse/+/ota/status', { qos: 1 });
    const [topic, body, packet] = await replay;
    assert.equal(topic, 'mouse/aabbccddeeff/ota/status');
    assert.deepEqual(JSON.parse(body), status);
    assert.equal(packet.retain, true);
    const confirmation = once(web, 'message');
    await device.publishAsync(topic, JSON.stringify({ ...status, phase: 'confirmed' }), {
      qos: 1,
      retain: true,
    });
    assert.equal(JSON.parse((await confirmation)[1]).phase, 'confirmed');
  } finally {
    await Promise.all([device.endAsync(true), web.endAsync(true)]);
    await app.close();
  }
});
