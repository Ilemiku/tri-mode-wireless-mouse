/* 控制台只显示设备报告的事实，不把用户选中的档位冒充实际DPI。
 * 阅读顺序：连接按钮 -> MQTT消息回调 -> render -> 设置/升级按钮。
 * devices保存设备快照；pending关联DPI命令；otaRequests关联升级请求。
 * 下方的$只是getElementById的简写，不依赖jQuery。
 */
const $ = (id) => document.getElementById(id),
  devices = new Map(),
  pending = new Map();
let client;
let releases = [],
  otaRequests = new Map();
const otaPhases = {
  idle: '可以升级',
  awaiting_device: '已发送，等待设备接受',
  unknown: '未收到升级状态：结果未知，请先核对设备',
  queued: '设备已接受，尚未安装',
  connecting: '校验HTTPS服务器身份',
  downloading: '写入备用固件槽',
  verifying: '校验升级镜像',
  rebooting: '镜像已校验，正在重启（还未成功）',
  self_testing: '新固件自检中（还未成功）',
  confirmed: '升级成功：新版本自检通过',
  failed: '升级失败，未确认成功',
  rolled_back: '升级未通过，已回滚旧版本',
  interrupted: '上次升级中断，未确认成功',
  rollback_requested: '自检超时，准备回滚',
};
const otaActivePhases = new Set([
  'awaiting_device',
  'unknown',
  'queued',
  'connecting',
  'downloading',
  'verifying',
  'rebooting',
  'self_testing',
  'rollback_requested',
]);
function log(label, value) {
  $('events').textContent =
    `${new Date().toLocaleTimeString()} ${label} ${JSON.stringify(value)}\n` +
    $('events').textContent.slice(0, 12000);
}
function render() {
  const d = devices.get($('device').value),
    fresh = d && d.online && Date.now() - d.seen < 4000;
  $('online').textContent = fresh
    ? d.state.ze_online
      ? 'ESP32与ZE均在线'
      : 'ESP32在线，ZE状态过期'
    : '设备离线或状态已过期';
  $('actual').textContent = fresh && d.state.ze_online ? d.state.dpi : '—';
  $('register').textContent =
    fresh && d.state.ze_online
      ? `Config1 = 0x${Number(d.state.register).toString(16).padStart(2, '0')}`
      : '等待ZE寄存器读回';
  $('mode').textContent = fresh ? ['有线USB', '私有2.4G', 'BLE'][d.state.mode] || '未知' : '—';
  $('version').textContent = `固件版本 ${fresh ? d.state.version : '—'}`;
  const busy = [...pending.values()].some((p) => p.device === $('device').value);
  $('apply').disabled = $('query').disabled = !(
    client?.connected &&
    fresh &&
    d.state.ze_online &&
    !busy
  );
  const pkg = releases.find((p) => p.sha256 === $('release').value);
  $('upload').disabled = !client?.connected || !$('firmware').files.length;
  $('upgrade').disabled = !(
    client?.connected &&
    fresh &&
    d.state.ze_online &&
    d.state.ota_capable &&
    !d.state.ota_busy &&
    !otaActivePhases.has(d.ota?.phase) &&
    pkg &&
    pkg.version !== d.state.version &&
    !otaRequests.has($('device').value)
  );
  if (d?.ota) {
    $('ota-status').textContent = otaPhases[d.ota.phase] || d.ota.phase;
    $('ota-progress').value = d.ota.percent || 0;
    $('ota-detail').textContent =
      `目标 ${d.ota.version || '—'} · 已报告运行版本 ${d.ota.running_version || '—'} · ${d.ota.partition || '—'} · ${d.ota.detail || ''}`;
  } else
    $('ota-status').textContent =
      fresh && d.state.ota_capable
        ? `OTA基础固件已上线 · ${d.state.partition}`
        : '等待支持OTA的固件上线。';
}
async function loadReleases() {
  const r = await fetch('/api/firmware', {
    headers: { Authorization: `Bearer ${$('token').value}` },
  });
  if (!r.ok) throw new Error('升级包服务不可用：' + r.status);
  releases = await r.json();
  const before = $('release').value;
  $('release').replaceChildren();
  for (const p of releases)
    $('release').add(
      new Option(
        `${p.version} · ${(p.size / 1024).toFixed(0)} KiB · ${p.sha256.slice(0, 8)}`,
        p.sha256,
      ),
    );
  if (!releases.length) $('release').add(new Option('尚无升级包', ''));
  else if (releases.some((p) => p.sha256 === before)) $('release').value = before;
  render();
}
$('connect').onclick = () => {
  client?.end(true);
  devices.clear();
  $('device').replaceChildren(new Option('等待设备上报', ''));
  client = mqtt.connect(
    `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/mqtt`,
    {
      protocolVersion: 4,
      username: 'mouse',
      password: $('token').value,
      clientId: 'web-' + crypto.randomUUID().slice(0, 12),
      clean: true,
      reconnectPeriod: 2000,
    },
  );
  client.on('connect', () => {
    $('connection').textContent = 'MQTT管理服务已连接';
    client.subscribe(
      ['mouse/+/state', 'mouse/+/result', 'mouse/+/availability', 'mouse/+/ota/status'],
      { qos: 1 },
    );
    loadReleases().catch((e) => {
      $('ota-package').textContent = e.message;
    });
  });
  client.on('offline', () => {
    $('connection').textContent = '管理服务已断开';
    render();
  });
  client.on('error', (e) => {
    $('connection').textContent = '连接失败：' + e.message;
  });
  client.on('message', (topic, bytes) => {
    let v;
    try {
      v = JSON.parse(bytes.toString());
    } catch {
      return;
    }
    const [, id, kind] = topic.split('/');
    if (!id) return;
    let d = devices.get(id);
    if (!d) {
      d = { online: false, state: {}, seen: 0 };
      devices.set(id, d);
      $('device').add(new Option(id, id));
      if (!$('device').value) $('device').value = id;
    }
    if (kind === 'availability') d.online = v.online === true;
    if (kind === 'state') {
      d.state = v;
      d.seen = Date.now();
    }
    if (kind === 'ota') {
      d.ota = v;
      log(topic, v);
      const p = otaRequests.get(id);
      if (p && p.id === v.id) {
        clearTimeout(p.timer);
        otaRequests.delete(id);
      }
    }
    if (kind === 'result') {
      log(topic, v);
      const p = pending.get(v.id);
      const upgrade = otaRequests.get(id);
      if (upgrade?.id === v.id && v.status !== 'accepted') {
        clearTimeout(upgrade.timer);
        otaRequests.delete(id);
        d.ota = { phase: 'failed', detail: `${v.status}: ${v.detail}`, version: upgrade.version };
      }
      if (p && p.device === id) {
        if (v.status === 'accepted') $('result').textContent = 'ESP32已接受，等待ZE执行…';
        else {
          clearTimeout(p.timer);
          pending.delete(v.id);
          $('result').textContent =
            v.status === 'applied'
              ? `已生效：${v.dpi} DPI，传感器读回0x${Number(v.register).toString(16)}`
              : `未确认生效：${v.status} / ${v.detail}`;
        }
      }
    }
    render();
  });
};
function send(op) {
  if (!client?.connected || !$('device').value) return;
  const id = crypto.randomUUID(),
    device = $('device').value,
    cmd = { id, op };
  if (op === 'set_dpi') cmd.dpi = Number($('dpi').value);
  const timer = setTimeout(() => {
    pending.delete(id);
    $('result').textContent = '等待执行结果超时：不能确认设置是否生效，请重新读取。';
    render();
  }, 4500);
  pending.set(id, { device, timer });
  $('result').textContent = '正在向设备下发…';
  render();
  log('发送', cmd);
  client.publish(
    `mouse/${device}/command`,
    JSON.stringify(cmd),
    { qos: 1, retain: false },
    (err) => {
      if (err) {
        clearTimeout(timer);
        pending.delete(id);
        $('result').textContent = '发送失败：' + err.message;
        render();
      } else log('PUBACK', { id, note: 'Broker收到，不代表传感器执行' });
    },
  );
}
$('apply').onclick = () => send('set_dpi');
$('query').onclick = () => send('get_state');
$('device').onchange = render;
setInterval(render, 1000);
$('firmware').onchange = render;
$('release').onchange = render;
$('upload').onclick = async () => {
  const file = $('firmware').files[0];
  if (!file) return;
  if (file.size > 2 * 1024 * 1024) {
    $('ota-package').textContent = '文件超过2MiB，不能装进单个OTA槽。';
    return;
  }
  $('upload').disabled = true;
  try {
    const r = await fetch('/api/firmware', {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${$('token').value}`,
        'Content-Type': 'application/octet-stream',
      },
      body: file,
    });
    const result = await r.json();
    if (!r.ok) throw new Error(result.error || '上传失败');
    await loadReleases();
    $('release').value = result.sha256;
    $('ota-package').textContent =
      `已检查：${result.project} / ${result.version} / ${result.size}字节 / SHA256 ${result.sha256}`;
  } catch (e) {
    $('ota-package').textContent = e.message;
  } finally {
    render();
  }
};
$('upgrade').onclick = () => {
  const pkg = releases.find((p) => p.sha256 === $('release').value),
    device = $('device').value;
  if (!pkg || !device || !client?.connected) return;
  if (!confirm(`升级ESP32至 ${pkg.version}？BLE会短暂断开；新固件自检通过才算成功。`)) return;
  const id = crypto.randomUUID(),
    cmd = {
      id,
      op: 'ota_start',
      url: pkg.url,
      version: pkg.version,
      sha256: pkg.sha256,
      size: pkg.size,
    };
  const timer = setTimeout(() => {
    otaRequests.delete(device);
    const d = devices.get(device);
    if (d)
      d.ota = {
        id,
        version: pkg.version,
        phase: 'unknown',
        detail: '请先核对当前版本与设备状态，不要连续触发',
      };
    render();
  }, 10000);
  otaRequests.set(device, { id, version: pkg.version, timer });
  devices.get(device).ota = {
    id,
    version: pkg.version,
    phase: 'awaiting_device',
    detail: '已发送升级请求，尚未收到设备确认',
  };
  render();
  log('OTA请求', cmd);
  client.publish(
    `mouse/${device}/command`,
    JSON.stringify(cmd),
    { qos: 1, retain: false },
    (err) => {
      if (err) {
        clearTimeout(timer);
        otaRequests.delete(device);
        const d = devices.get(device);
        if (d)
          d.ota = {
            id,
            version: pkg.version,
            phase: 'unknown',
            detail: '请求发送异常：' + err.message,
          };
        render();
      }
    },
  );
};
