/*
 * options.js - the options page controller.
 */

const DEFAULTS = {
  port: 8899,
  interceptDownloads: true,
  minSizeKB: 0,
  ignoreExtensions: '',
  ignoreHosts: '',
  useWebSocket: true,
  notifyOnComplete: true,
  sendReferer: true
};

const TEXT_FIELDS = ['ignoreExtensions', 'ignoreHosts'];
const NUMBER_FIELDS = ['port', 'minSizeKB'];
const BOOL_FIELDS = ['interceptDownloads', 'useWebSocket', 'notifyOnComplete', 'sendReferer'];

const $ = (id) => document.getElementById(id);

function setState(text) {
  $('saveState').textContent = text;
  if (text) setTimeout(() => { $('saveState').textContent = ''; }, 2200);
}

async function load() {
  const stored = await chrome.storage.sync.get(DEFAULTS);
  for (const key of TEXT_FIELDS) $(key).value = stored[key] || '';
  for (const key of NUMBER_FIELDS) $(key).value = stored[key];
  for (const key of BOOL_FIELDS) $(key).checked = !!stored[key];
}

async function save() {
  const payload = {};
  for (const key of TEXT_FIELDS) payload[key] = $(key).value.trim();
  for (const key of NUMBER_FIELDS) {
    let value = parseInt($(key).value, 10);
    if (!isFinite(value) || value < 0) value = DEFAULTS[key];
    if (key === 'port') value = Math.min(65535, Math.max(1024, value));
    payload[key] = value;
  }
  for (const key of BOOL_FIELDS) payload[key] = $(key).checked;

  await chrome.storage.sync.set(payload);
  setState('已保存 ✓');

  // Ask the worker to reconnect with the new port/transport.
  chrome.runtime.sendMessage({ type: 'settings-changed' }, () => {
    if (chrome.runtime.lastError) { /* worker may be asleep */ }
  });
}

async function testConnection() {
  $('connState').textContent = '正在检测…';
  await save();
  chrome.runtime.sendMessage({ type: 'status' }, (reply) => {
    if (chrome.runtime.lastError || !reply) {
      $('connState').textContent = '无法与扩展后台通信';
      return;
    }
    if (reply.connected) {
      $('connState').textContent = '已连接 · aria2 ' + (reply.version || '未知版本')
        + ' · 端口 ' + reply.port;
    } else {
      $('connState').textContent = reply.error || '未连接，请确认桌面程序正在运行';
    }
  });
}

document.addEventListener('DOMContentLoaded', () => {
  load();
  $('save').addEventListener('click', save);
  $('test').addEventListener('click', testConnection);
  $('reset').addEventListener('click', async () => {
    await chrome.storage.sync.set(DEFAULTS);
    await load();
    setState('已恢复默认');
    chrome.runtime.sendMessage({ type: 'settings-changed' }, () => {});
  });
});
