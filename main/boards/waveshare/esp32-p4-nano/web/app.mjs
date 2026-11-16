import { ModbusSlave, RtuDecoder, withCrc, hex, parseHex } from './protocol.mjs';
import { SerialSession } from './serial.mjs';
import { GapFramer, defaultFrameGap } from './framing.mjs';
import {
  REGISTER_COUNT, BUSINESS_FIELDS, createPresetRegisters, readField, writeRaw,
  describeRegister, formatRegister,
} from './registers.mjs';

const $ = id => document.getElementById(id);
const slave = new ModbusSlave();
const key = 'p4-nano-modbus-lab-v2';
const names = new Map();
for (let address = 0; address < REGISTER_COUNT; address++) names.set(address, describeRegister(address));
let session = null, connecting = false, responseDelay = 0;
let counts = { rx: 0, tx: 0, error: 0 };
let logs = [];
const rows = [];
const meanings = [];

function notice(message, error = false) { $('notice').textContent = message; $('notice').classList.toggle('error', error); }
function integer(value, min, max) {
  if (String(value).trim() === '') throw new Error('请输入数值');
  const n = Number(value);
  if (!Number.isInteger(n) || n < min || n > max) throw new Error(`请输入 ${min} 至 ${max} 的整数`);
  return n;
}
function updateCounts() { for (const k of ['rx', 'tx', 'error']) $(`${k}-count`).textContent = counts[k]; }
function record(direction, bytes, summary, offline = false) {
  const entry = { time: new Date().toISOString(), direction, hex: hex(bytes), summary, offline };
  logs.push(entry);
  if (logs.length > 300) logs.shift();
  const container = $('log');
  container.querySelector('.empty')?.remove();
  const row = document.createElement('div'); row.className = 'log-row';
  const meta = document.createElement('div'); meta.className = 'log-meta';
  meta.textContent = new Date(entry.time).toLocaleTimeString('zh-CN', { hour12: false }) + '.' + entry.time.slice(20, 23);
  const badge = document.createElement('span'); badge.className = `tag ${direction === 'TX' ? 'tx' : direction === 'DROP' ? 'warn' : ''}`;
  badge.textContent = `${offline ? '离线 ' : ''}${direction}`;
  meta.append(document.createElement('br'), badge);
  const body = document.createElement('div'); const raw = document.createElement('div'); raw.className = 'hex'; raw.textContent = entry.hex || '—';
  const decoded = document.createElement('div'); decoded.className = 'decoded'; decoded.textContent = summary;
  body.append(raw, decoded); row.append(meta, body); container.append(row);
  while (container.children.length > 300) container.firstElementChild.remove();
  if ($('follow').checked) container.scrollTop = container.scrollHeight;
}
function settings() { return { version: 1, address: slave.address, registers: Array.from(slave.registers) }; }
function persist() {
  try { localStorage.setItem(key, JSON.stringify(settings())); $('saved').textContent = '已保存在当前浏览器'; }
  catch { $('saved').textContent = '浏览器存储不可用，请导出设置'; }
}
function loadSettings(data) {
  if (!data || data.version !== 1 || !Array.isArray(data.registers) || data.registers.length !== 167) throw new Error('配置需为 version:1，包含 167 个 registers');
  const address = integer(data.address, 1, 247);
  const regs = data.registers.map(n => { if (typeof n !== 'number') throw new Error('寄存器必须是数字'); return integer(n, 0, 65535); });
  if (session || connecting) throw new Error('请先断开串口再导入设置');
  slave.registers.set(regs); slave.address = address; $('address').value = address;
}
function preset() {
  // 完整的产线示例快照(152 个上报字段全部有值), 见 registers.mjs 的 createPresetRegisters().
  slave.registers.set(createPresetRegisters());
}
function sync(changed = []) {
  // Keep the authoritative register bank visible after either serial or local writes.
  for (const field of BUSINESS_FIELDS) {
    const input = $(`field-${field.id}`);
    if (input) input.value = readField(slave.registers, field.id);
  }
  rows.forEach(({ tr, input, raw }, address) => {
    input.value = slave.registers[address]; raw.textContent = `0x${slave.registers[address].toString(16).toUpperCase().padStart(4, '0')}`;
    const meaning = formatRegister(address, slave.registers);
    if (meanings[address]) meanings[address].textContent = meaning ? `= ${meaning}` : '';
    tr.classList.toggle('changed', changed.includes(address));
  });
}
function processFrame(frame, offline = false) {
  // Pausing responses also pauses writes, simulating an unresponsive slave.
  if (!offline && !$('respond').checked) {
    ++counts.rx; record('RX', frame, '自动响应已关闭：仅监视，未执行请求'); updateCounts(); return;
  }
  const result = slave.handle(frame);
  if (!offline) { ++counts.rx; if (result.reason && !result.changed.length) ++counts.error; }
  record('RX', frame, `地址 ${frame[0]} · 功能 0x${(frame[1] ?? 0).toString(16).padStart(2, '0')} · ${result.summary}${result.response ? ' · CRC 正确' : ''}`, offline);
  if (result.changed.length) { sync(result.changed); persist(); }
  if (result.response) {
    const explanation = result.response[1] & 0x80 ? `异常响应 · 代码 ${result.response[2]}` : result.response[1] === 3 ? `读取返回 · ${result.response[2] / 2} 个寄存器 · 值 [${Array.from({ length: result.response[2] / 2 }, (_, i) => result.response[3 + i * 2] * 256 + result.response[4 + i * 2]).join(', ')}] · CRC 正确` : '写入确认 · 原样回显请求 · CRC 正确';
    if (offline) record('TX', result.response, explanation, true);
    else {
      const target = session;
      target?.send(result.response, responseDelay, () => {
        ++counts.tx; updateCounts(); record('TX', result.response, explanation);
      }).catch(error => { ++counts.error; updateCounts(); notice(`响应发送失败：${error.message}`, true); });
    }
  }
  updateCounts();
}
const decoder = new RtuDecoder(frame => processFrame(frame), (bytes, reason) => {
  ++counts.error; updateCounts(); record('DROP', bytes, `${reason}；未响应`);
});
const framer = new GapFramer(decoder);
function setConnected(connected) {
  $('connect').disabled = connected || connecting; $('disconnect').disabled = !connected;
  $('address').disabled = connected || connecting; $('baud').disabled = connected || connecting;
  $('frame-gap').disabled = connected || connecting;
  $('status').textContent = connected ? `● 从机 ${slave.address} 在线` : '● 未连接';
  $('status').classList.toggle('active', connected);
}
$('connection').addEventListener('submit', async event => {
  event.preventDefault();
  if (session || connecting) return;
  if (!('serial' in navigator) || !isSecureContext) { notice('此环境无法使用 Web Serial，请用桌面 Chrome / Edge 打开 localhost 或 HTTPS 页面。', true); return; }
  connecting = true; setConnected(false);
  let candidate, connectionError = false;
  try {
    slave.address = integer($('address').value, 1, 247); persist();
    const port = await navigator.serial.requestPort();
    framer.setGap(integer($('frame-gap').value, 1, 1000));
    candidate = new SerialSession(bytes => { if (candidate.active) framer.push(bytes); }, error => { framer.reset(); connectionError = true; notice(`串口错误：${error.message}`, true); }, () => {
      framer.reset();
      if (session === candidate) { session = null; setConnected(false); if (!connectionError) notice('串口已断开'); }
    });
    session = candidate;
    await candidate.open(port, Number($('baud').value));
    notice('串口已连接，等待单片机主动发起读写。当前固件不自动轮询。');
  } catch (error) {
    if (candidate) await candidate.close();
    session = null; notice(error.name === 'NotFoundError' ? '已取消选择串口' : `连接失败：${error.message}`, error.name !== 'NotFoundError');
  } finally { connecting = false; setConnected(Boolean(session?.active)); }
});
$('disconnect').onclick = async () => { $('disconnect').disabled = true; framer.reset(); await session?.close(); };
$('baud').onchange = () => {
  const gap = defaultFrameGap(Number($('baud').value));
  $('frame-gap').value = gap; framer.setGap(gap);
};
$('frame-gap').onchange = () => {
  try { framer.setGap(integer($('frame-gap').value, 1, 1000)); }
  catch (e) { $('frame-gap').value = framer.gapMs; notice(e.message, true); }
};
$('address').onchange = () => { try { slave.address = integer($('address').value, 1, 247); persist(); } catch (e) { notice(e.message, true); $('address').value = slave.address; } };
$('delay').onchange = () => { try { responseDelay = integer($('delay').value, 0, 3000); } catch (e) { $('delay').value = responseDelay; notice(e.message, true); } };

// 业务字段按寄存器目录自动生成: 每个可读字段一个输入框, 提交后直接写共享寄存器.
for (const field of BUSINESS_FIELDS) {
  const label = document.createElement('label');
  label.textContent = field.name;
  const hexAddr = `0x${field.addr.toString(16).toUpperCase().padStart(2, '0')}`;
  const small = document.createElement('small');
  small.textContent = field.type === 's32' || field.type === 'u32'
    ? `${hexAddr} / 0x${(field.addr + 1).toString(16).toUpperCase().padStart(2, '0')} · ${field.type}`
    : `${hexAddr}${field.scale && field.scale !== 1 ? ` × ${field.scale}` : ''} · ${field.type}`;
  const input = document.createElement('input');
  const range = field.view === 'percent' ? { min: '0', max: '100', step: '1' }
    : field.view === 'valve' ? { min: '0', max: '2', step: '1' }
      : field.type === 's32' ? { min: '-2147483648', max: '2147483647', step: '1' }
        : { min: '0', max: '65535', step: field.scale && field.scale !== 1 ? String(field.scale) : '1' };
  Object.assign(input, { id: `field-${field.id}`, type: 'number', required: true, ...range });
  input.dataset.field = field.id;
  label.append(small, input); $('fields').append(label);
}
$('values').onsubmit = event => event.preventDefault();
$('values').onchange = event => {
  const input = event.target;
  const fieldId = input.dataset?.field;
  if (!fieldId) return;
  const field = BUSINESS_FIELDS.find(item => item.id === fieldId);
  try {
    if (!input.checkValidity()) throw new Error('数值超出范围或精度不正确');
    // 输入框里是物理量(如 25.500 °C), 按倍率换算回原始寄存器值.
    const raw = field.scale && field.scale !== 1
      ? integer(Math.round(Number(input.value) / field.scale), 0, Number(input.max))
      : integer(input.value, Number(input.min), Number(input.max));
    const changed = writeRaw(slave.registers, field.type, field.addr, raw);
    sync(changed); persist();
  } catch (error) { notice(error.message, true); sync(); }
};
for (let address = 0; address < REGISTER_COUNT; address++) {
  const tr = document.createElement('tr');
  const cell = text => { const td = document.createElement('td'); td.textContent = text; tr.append(td); return td; };
  cell(`${address} / 0x${address.toString(16).toUpperCase().padStart(2, '0')}`);
  cell(names.get(address) || '通用保持寄存器');
  const meaning = document.createElement('small'); meaning.className = 'decoded';
  meanings.push(meaning); tr.lastElementChild.append(meaning);
  const input = document.createElement('input'); Object.assign(input, { type: 'number', min: '0', max: '65535', step: '1' }); input.setAttribute('aria-label', `寄存器 ${address} 原始值`);
  input.onchange = () => { try { slave.registers[address] = integer(input.value, 0, 65535); sync([address]); persist(); } catch (e) { notice(e.message, true); sync(); } };
  cell('').append(input); const raw = cell(''); $('registers').append(tr); rows.push({ tr, input, raw });
}
$('filter').oninput = () => {
  const q = $('filter').value.trim().toLowerCase();
  rows.forEach(({ tr }, a) => { tr.hidden = !(String(a).includes(q) || `0x${a.toString(16).padStart(2, '0')}`.includes(q) || (names.get(a) || '通用保持寄存器').toLowerCase().includes(q)); });
};
function download(name, data, type) {
  const url = URL.createObjectURL(new Blob([data], { type })); const a = document.createElement('a'); a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$('export').onclick = () => download('modbus-settings.json', JSON.stringify(settings(), null, 2), 'application/json');
$('import').onchange = async event => {
  try { const file = event.target.files[0]; if (!file) return; if (file.size > 100000) throw new Error('配置文件过大'); loadSettings(JSON.parse(await file.text())); sync(); persist(); notice('配置已导入'); }
  catch (e) { notice(`导入失败：${e.message}`, true); }
  finally { event.target.value = ''; }
};
$('reset').onclick = () => { preset(); sync(); persist(); notice('已恢复示例数据'); };
$('clear-log').onclick = () => { logs = []; $('log').replaceChildren(); counts = { rx: 0, tx: 0, error: 0 }; updateCounts(); };
$('download-log').onclick = () => download('modbus-traffic.json', JSON.stringify(logs, null, 2), 'application/json');
const samples = { temperature: [3, 0, 108, 0, 1], lights: [3, 0, 59, 0, 2], weight: [3, 0, 2, 0, 2], valve: [6, 0, 107, 0, 1] };
document.querySelectorAll('[data-sample]').forEach(button => { button.onclick = () => { $('frame').value = hex(withCrc([slave.address, ...samples[button.dataset.sample]])); }; });
$('inject-form').onsubmit = event => {
  event.preventDefault();
  try { const frame = parseHex($('frame').value); if (frame.length < 4 || frame.length > 256) throw new Error('RTU 帧长度应为 4–256 字节'); processFrame(frame, true); }
  catch (e) { notice(`报文无效：${e.message}`, true); }
};
if (!('serial' in navigator) || !isSecureContext) $('support').textContent = '当前环境不支持 Web Serial；离线模拟可用。实际连接请用桌面 Chrome / Edge 与 localhost / HTTPS。';
preset();
try { const saved = localStorage.getItem(key); if (saved) loadSettings(JSON.parse(saved)); }
catch (e) { notice(`保存的设置不可用，已使用示例值：${e.message}`, true); }
sync();
$('frame').value = hex(withCrc([slave.address, ...samples.temperature]));
