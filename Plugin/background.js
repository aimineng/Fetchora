/*
 * background.js - MV3 service worker for the Fetchora bridge.
 *
 * Two transports are supported:
 *   1. WebSocket  ws://127.0.0.1:<port>/ws   (primary: instant, bidirectional)
 *   2. HTTP POST  http://127.0.0.1:<port>/...  (fallback)
 *
 * The worker prefers the socket and silently degrades to HTTP, so the extension
 * also works with an older build of the desktop application.
 */

const DEFAULTS = {
  port: 8899,
  interceptDownloads: true,
  minSizeKB: 0,            // 0 = take everything
  ignoreExtensions: '',    // comma separated, e.g. "pdf,html"
  ignoreHosts: '',         // comma separated host fragments
  useWebSocket: true,
  notifyOnComplete: true,
  sendReferer: true
};

let settings = { ...DEFAULTS };
let socket = null;
let socketReady = false;
let reconnectTimer = null;
const pendingRequests = new Map(); // id -> { resolve, timer }
let requestSeq = 0;
let lastError = '';

// ---------------------------------------------------------------- settings

async function loadSettings() {
  const stored = await chrome.storage.sync.get(DEFAULTS);
  settings = { ...DEFAULTS, ...stored };
  return settings;
}

chrome.storage.onChanged.addListener((changes, area) => {
  if (area !== 'sync' && area !== 'local') return;
  let touched = false;
  for (const key of Object.keys(changes)) {
    if (key in DEFAULTS) {
      settings[key] = changes[key].newValue;
      touched = true;
    }
  }
  if (touched) {
    closeSocket();
    connectSocket();
  }
});

// --------------------------------------------------------------- transport

function baseUrl() {
  return 'http://127.0.0.1:' + settings.port;
}

function wsUrl() {
  return 'ws://127.0.0.1:' + settings.port + '/ws';
}

function closeSocket() {
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (socket) {
    try { socket.close(); } catch (e) { /* ignore */ }
    socket = null;
  }
  socketReady = false;
}

function scheduleReconnect(delay) {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectSocket();
  }, delay || 3000);
}

function connectSocket() {
  if (!settings.useWebSocket || socket) return;
  try {
    socket = new WebSocket(wsUrl());
  } catch (e) {
    socket = null;
    scheduleReconnect();
    return;
  }

  socket.addEventListener('open', () => {
    socketReady = true;
    lastError = '';
  });

  socket.addEventListener('message', (event) => {
    let payload;
    try {
      payload = JSON.parse(event.data);
    } catch (e) {
      return;
    }
    if (payload.id && pendingRequests.has(payload.id)) {
      const entry = pendingRequests.get(payload.id);
      pendingRequests.delete(payload.id);
      clearTimeout(entry.timer);
      entry.resolve(payload);
      return;
    }
    if (payload.event === 'task-completed' && settings.notifyOnComplete) {
      notify('下载完成', payload.name || '任务已完成');
    }
  });

  socket.addEventListener('close', () => {
    socketReady = false;
    socket = null;
    scheduleReconnect();
  });

  socket.addEventListener('error', () => {
    socketReady = false;
    lastError = '无法连接桌面程序';
  });
}

function socketRequest(command, timeoutMs) {
  return new Promise((resolve) => {
    if (!socketReady || !socket) {
      resolve(null);
      return;
    }
    const id = 'ext-' + (++requestSeq);
    const timer = setTimeout(() => {
      pendingRequests.delete(id);
      resolve(null);
    }, timeoutMs || 1500);
    pendingRequests.set(id, { resolve, timer });
    try {
      socket.send(JSON.stringify(Object.assign({}, command, { id })));
    } catch (e) {
      clearTimeout(timer);
      pendingRequests.delete(id);
      resolve(null);
    }
  });
}

async function httpRequest(path, body, timeoutMs) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs || 1500);
  try {
    const response = await fetch(baseUrl() + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
      signal: controller.signal
    });
    clearTimeout(timer);
    if (!response.ok) {
      lastError = 'HTTP ' + response.status;
      return null;
    }
    lastError = '';
    return await response.json().catch(() => ({}));
  } catch (e) {
    clearTimeout(timer);
    lastError = '桌面程序未运行';
    return null;
  }
}

/** Sends a command over the best available transport; null on failure. */
async function send(path, body) {
  if (settings.useWebSocket) {
    const command = mapPathToCommand(path, body);
    if (command) {
      const result = await socketRequest(command);
      if (result && result.ok) return result;
      if (result && result.error) {
        lastError = result.error;
        return null;
      }
    }
  }
  return httpRequest(path, body);
}

function mapPathToCommand(path, body) {
  switch (path) {
    case '/download':
      return { cmd: 'add', url: body.url, options: body.options || {}, filename: body.filename };
    case '/add':
      return { cmd: 'add', urls: body.urls, options: body.options || {} };
    case '/torrent':
      return { cmd: 'torrent', torrent: body.torrent };
    case '/magnet':
      return { cmd: 'magnet', magnet: body.magnet };
    case '/pause':
      return { cmd: 'pause', gid: body ? body.gid : '' };
    case '/unpause':
      return { cmd: 'unpause', gid: body ? body.gid : '' };
    case '/ping':
      return { cmd: 'ping' };
    default:
      return null;
  }
}

async function checkConnection() {
  const result = await send('/ping', {});
  const ok = !!(result && (result.ok || result.app));
  return { ok, version: ok ? (result.version || '') : '', error: ok ? '' : lastError };
}

// --------------------------------------------------------------- filtering

function hostOf(url) {
  try {
    return new URL(url).hostname.toLowerCase();
  } catch (e) {
    return '';
  }
}

function extensionOf(filename, url) {
  const source = (filename || url || '').split('?')[0];
  const idx = source.lastIndexOf('.');
  if (idx < 0) return '';
  return source.substring(idx + 1).toLowerCase();
}

/** Decides whether a browser download should be handed to aria2. */
function shouldIntercept(item) {
  if (!settings.interceptDownloads) return false;

  const url = item.url || '';
  if (!/^https?:/i.test(url)) return false;
  if (/^(blob|data|filesystem):/i.test(url)) return false;

  const host = hostOf(url);
  const ignoredHosts = settings.ignoreHosts.split(',')
    .map(s => s.trim().toLowerCase()).filter(Boolean);
  if (ignoredHosts.some(h => host.indexOf(h) >= 0)) return false;

  const ext = extensionOf(item.filename, url);
  const ignoredExt = settings.ignoreExtensions.split(',')
    .map(s => s.trim().toLowerCase()).filter(Boolean);
  if (ext && ignoredExt.indexOf(ext) >= 0) return false;

  const size = Number(item.totalBytes || item.fileSize || 0);
  if (settings.minSizeKB > 0 && size > 0 && size < settings.minSizeKB * 1024) return false;

  return true;
}

function buildOptions(item) {
  const options = {};
  if (settings.sendReferer && item.referrer) options.referer = item.referrer;
  if (item.filename) options.out = item.filename.split(/[\\/]/).pop();
  if (item.mime) options['content-type'] = item.mime;
  return options;
}

// --------------------------------------------------------------- downloads

chrome.downloads.onDeterminingFilename.addListener((item, suggest) => {
  if (!shouldIntercept(item)) {
    suggest();
    return false;
  }

  handleIntercept(item, suggest);
  return true; // we answer asynchronously
});

async function handleIntercept(item, suggest) {
  const result = await send('/download', {
    url: item.url,
    filename: item.filename,
    referer: item.referrer,
    origin: 'chrome',
    options: buildOptions(item)
  });

  if (result && result.ok) {
    chrome.downloads.cancel(item.id, () => {
      if (chrome.runtime.lastError) {
        // The download was already gone; nothing else to do.
        return;
      }
      chrome.downloads.erase({ id: item.id });
    });
    return;
  }

  // Hand the download back to the browser.
  suggest();
}

// ----------------------------------------------------------- context menus

function createMenus() {
  chrome.contextMenus.removeAll(() => {
    const items = [
      { id: 'aria2-link', title: '用 Fetchora 下载此链接', contexts: ['link'] },
      { id: 'aria2-media', title: '用 Fetchora 下载此媒体', contexts: ['video', 'audio'] },
      { id: 'aria2-image', title: '用 Fetchora 下载此图片', contexts: ['image'] },
      { id: 'aria2-page', title: '用 Fetchora 下载当前页面', contexts: ['page'] },
      { id: 'aria2-magnet', title: '发送选中的磁力链接', contexts: ['selection'] },
      { id: 'sep-1', type: 'separator', contexts: ['page', 'link'] },
      {
        id: 'aria2-toggle',
        title: settings.interceptDownloads ? '暂停自动接管下载' : '恢复自动接管下载',
        contexts: ['page', 'link']
      },
      { id: 'aria2-options', title: '扩展设置…', contexts: ['page', 'link'] }
    ];
    for (const item of items) {
      if (item.type === 'separator') chrome.contextMenus.create(item);
      else chrome.contextMenus.create(item);
    }
  });
}

chrome.runtime.onInstalled.addListener(() => {
  loadSettings().then(() => {
    createMenus();
    connectSocket();
  });
});

chrome.runtime.onStartup.addListener(() => {
  loadSettings().then(() => {
    createMenus();
    connectSocket();
  });
});

chrome.contextMenus.onClicked.addListener(async (info) => {
  await loadSettings();
  const base = { origin: 'chrome-context' };

  switch (info.menuItemId) {
    case 'aria2-link':
      if (info.linkUrl) await send('/download', Object.assign({}, base, { url: info.linkUrl, referer: info.pageUrl }));
      break;
    case 'aria2-media':
    case 'aria2-image':
      if (info.srcUrl) await send('/download', Object.assign({}, base, { url: info.srcUrl, referer: info.pageUrl }));
      break;
    case 'aria2-page':
      if (info.pageUrl) await send('/download', Object.assign({}, base, { url: info.pageUrl, referer: info.pageUrl }));
      break;
    case 'aria2-magnet': {
      const text = (info.selectionText || '').trim();
      const match = text.match(/magnet:\?[^\s"']+/i);
      if (match) await send('/magnet', { magnet: match[0] });
      else notify('未找到磁力链接', '选中的文本里没有 magnet: 链接');
      break;
    }
    case 'aria2-toggle':
      settings.interceptDownloads = !settings.interceptDownloads;
      await chrome.storage.sync.set({ interceptDownloads: settings.interceptDownloads });
      createMenus();
      notify('Fetchora',
             settings.interceptDownloads ? '已恢复自动接管下载' : '已暂停自动接管下载');
      break;
    case 'aria2-options':
      chrome.runtime.openOptionsPage();
      break;
    default:
      break;
  }
});

// -------------------------------------------------------------- keep-alive

chrome.alarms.create('aria2-keepalive', { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === 'aria2-keepalive' && !socketReady) connectSocket();
});

// ---------------------------------------------------------------- messaging

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  (async () => {
    await loadSettings();
    switch (message.type) {
      case 'status': {
        const conn = await checkConnection();
        sendResponse({
          connected: conn.ok,
          version: conn.version,
          error: conn.error,
          port: settings.port,
          intercept: settings.interceptDownloads
        });
        break;
      }
      case 'add-url': {
        const result = await send('/download', {
          url: message.url,
          referer: message.referer,
          options: message.options || {}
        });
        sendResponse({ ok: !!(result && result.ok), error: lastError });
        break;
      }
      case 'add-magnet': {
        const result = await send('/magnet', { magnet: message.magnet });
        sendResponse({ ok: !!(result && result.ok), error: lastError });
        break;
      }
      case 'add-torrent': {
        const result = await send('/torrent', { torrent: message.torrent });
        sendResponse({ ok: !!(result && result.ok), error: lastError });
        break;
      }
      case 'pause-all': {
        const result = await send('/pause', {});
        sendResponse({ ok: !!(result && result.ok), error: lastError });
        break;
      }
      case 'resume-all': {
        const result = await send('/unpause', {});
        sendResponse({ ok: !!(result && result.ok), error: lastError });
        break;
      }
      case 'toggle-intercept': {
        settings.interceptDownloads = !settings.interceptDownloads;
        await chrome.storage.sync.set({ interceptDownloads: settings.interceptDownloads });
        createMenus();
        sendResponse({ ok: true, intercept: settings.interceptDownloads });
        break;
      }
      case 'settings-changed': {
        closeSocket();
        connectSocket();
        sendResponse({ ok: true });
        break;
      }
      default:
        sendResponse({ ok: false, error: 'unknown message' });
    }
  })();
  return true; // keep the channel open for the async response
});

function notify(title, message) {
  try {
    chrome.notifications.create({
      type: 'basic',
      iconUrl: 'icons/icon128.png',
      title,
      message: message || ''
    });
  } catch (e) { /* notifications may be unavailable */ }
}

// Boot.
loadSettings().then(() => {
  createMenus();
  connectSocket();
});
