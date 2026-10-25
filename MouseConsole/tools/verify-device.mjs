// 实物验收工具：默认只查询；--write才临时切换DPI并恢复原值。
import mqtt from 'mqtt';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { once } from 'node:events';

const device = process.env.MOUSE_DEVICE || '28848560da8c';
if (!/^[a-f0-9]{12}$/.test(device)) throw new Error('Invalid device ID');
const token = (await readFile(new URL('../.local/token', import.meta.url), 'utf8')).trim();
const client = mqtt.connect(process.env.MQTT_URL || 'mqtt://127.0.0.1:18830', {
  username: 'mouse',
  password: token,
  protocolVersion: 4,
  reconnectPeriod: 0,
  connectTimeout: 4000,
});
const pending = new Map();
client.on('message', (topic, payload) => {
  if (topic !== `mouse/${device}/result`) return;
  let result;
  try {
    result = JSON.parse(payload);
  } catch {
    return;
  }
  const entry = pending.get(result.id);
  if (!entry) return;
  console.log(JSON.stringify(result));
  if (result.status === 'accepted') return;
  clearTimeout(entry.timer);
  pending.delete(result.id);
  if (result.status === 'applied') entry.resolve(result);
  else entry.reject(new Error(`Device result: ${result.status}`));
});
async function request(op, dpi) {
  const id = randomUUID(),
    body = { id, op };
  if (dpi !== undefined) body.dpi = dpi;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error('No device result within 4 seconds'));
    }, 4000);
    pending.set(id, { resolve, reject, timer });
    client.publish(
      `mouse/${device}/command`,
      JSON.stringify(body),
      { qos: 1, retain: false },
      (error) => {
        if (error) {
          clearTimeout(timer);
          pending.delete(id);
          reject(error);
        }
      },
    );
  });
}
try {
  await once(client, 'connect');
  await client.subscribeAsync(`mouse/${device}/result`, { qos: 1 });
  const original = await request('get_state');
  if (process.argv.includes('--write')) {
    if (![800, 1600, 3200, 6400].includes(original.dpi))
      throw new Error('Original DPI invalid; refusing write');
    const target = original.dpi === 1600 ? 3200 : 1600;
    try {
      const changed = await request('set_dpi', target);
      if (changed.dpi !== target || changed.register !== target / 100 - 1)
        throw new Error('Sensor readback mismatch');
      console.log('Real DPI write/readback verified');
    } finally {
      const restored = await request('set_dpi', original.dpi);
      if (restored.dpi !== original.dpi) throw new Error('Original DPI restoration failed');
      console.log('Original DPI restored');
    }
  }
} finally {
  for (const item of pending.values()) clearTimeout(item.timer);
  await client.endAsync(true);
}
