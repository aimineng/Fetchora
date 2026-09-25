/*
 * popup.js - the extension popup controller.
 *
 * All privileged work happens in the service worker; the popup only renders
 * and forwards user intent through chrome.runtime.sendMessage.
 */

const $ = (id) => document.getElementById(id);

function formatBytes(bytes) {
  const b = Number(bytes || 0);
  if (!isFinite(b) || b <= 0) return "0 B";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  let v = b;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  return v.toFixed(i === 0 ? 0 : 2) + " " + units[i];
}

function formatSpeed(bytes) {
  const b = Number(bytes || 0);
  if (!isFinite(b) || b <= 0) return "--";
  return formatBytes(b) + "/s";
}

function send(message) {
  return new Promise((resolve) => {
    chrome.runtime.sendMessage(message, (reply) => {
      if (chrome.runtime.lastError) resolve({ ok: false, error: chrome.runtime.lastError.message });
      else resolve(reply || { ok: false, error: "无响应" });
    });
  });
}

async function refresh() {
  const status = await send({ type: "status" });
  const connected = !!(status && status.connected);

  $("dot").className = "dot" + (connected ? " on" : "");
  $("statusText").textContent = connected ? "已连接" : "未连接";
  $("version").textContent = connected
    ? "aria2 " + (status.version || "") + " · 端口 " + status.port
    : (status && status.error ? status.error : "桌面程序未运行");
  $("hint").textContent = "端口 " + ((status && status.port) || 8899);
  $("intercept").checked = !!(status && status.intercept);

  if (!connected) {
    $("downSpeed").textContent = "--";
    $("upSpeed").textContent = "--";
    $("activeCount").textContent = "0";
    $("waitingCount").textContent = "0";
    $("taskList").innerHTML = '<div class="muted">启动 Aria2 Downloader 后即可在这里查看任务。</div>';
  }
}

async function submitUrl() {
  const value = $("url").value.trim();
  if (!value) return;
  const button = $("send");
  button.disabled = true;
  button.textContent = "发送中";

  let reply;
  if (/^magnet:/i.test(value)) {
    reply = await send({ type: "add-magnet", magnet: value });
  } else {
    reply = await send({ type: "add-url", url: value });
  }

  button.disabled = false;
  button.textContent = reply && reply.ok ? "已发送" : "失败";
  if (reply && reply.ok) {
    $("url").value = "";
    setTimeout(() => { button.textContent = "发送"; }, 1200);
  } else {
    setTimeout(() => { button.textContent = "发送"; }, 1800);
  }
}

document.addEventListener("DOMContentLoaded", () => {
  refresh();
  setInterval(refresh, 2500);

  $("send").addEventListener("click", submitUrl);
  $("url").addEventListener("keydown", (e) => {
    if (e.key === "Enter") submitUrl();
  });

  $("paste").addEventListener("click", async () => {
    try {
      const text = await navigator.clipboard.readText();
      $("url").value = (text || "").trim();
    } catch (e) {
      $("url").placeholder = "无法读取剪贴板，请手动粘贴";
    }
  });

  $("currentPage").addEventListener("click", async () => {
    const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
    if (tabs && tabs[0] && tabs[0].url) {
      $("url").value = tabs[0].url;
      await send({ type: "add-url", url: tabs[0].url, referer: tabs[0].url });
      window.close();
    }
  });

  $("intercept").addEventListener("change", async () => {
    await send({ type: "toggle-intercept" });
  });

  $("pauseAll").addEventListener("click", async () => { await send({ type: "pause-all" }); refresh(); });
  $("resumeAll").addEventListener("click", async () => { await send({ type: "resume-all" }); refresh(); });
  $("options").addEventListener("click", () => chrome.runtime.openOptionsPage());
});
