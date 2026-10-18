#pragma once

#include <Arduino.h>

// Substituted at request time: %TITLE% (BaseConfig::title), %CHIP% (target
// chip), %APP_HTML% / %APP_JS% (BaseConfig::appHtml / appScript).
static const char BASE_PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
:root{--bg:#f3f5f8;--card:#fff;--fg:#1d2430;--muted:#667085;--line:#d9dee7;--accent:#1f6feb;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700}
@media (prefers-color-scheme:dark){:root{--bg:#0f1419;--card:#181f27;--fg:#e6edf3;--muted:#8b98a5;--line:#2b3440;--accent:#4493f8;--ok:#3fb950;--bad:#f85149;--warn:#d29922}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line);padding:14px 16px}
header .wrap{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px}
h1{font-size:19px;margin:0;flex:1 1 auto}
main.wrap{padding:16px}
.wrap{max-width:760px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
h2{font-size:15px;margin:0 0 12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}
.pill{font-size:12px;padding:2px 9px;border-radius:999px;border:1px solid var(--line);white-space:nowrap}
.pill.ok{color:var(--ok);border-color:var(--ok)}.pill.warn{color:var(--warn);border-color:var(--warn)}
dl{display:grid;grid-template-columns:max-content 1fr;gap:6px 16px;margin:0}
dt{color:var(--muted)}dd{margin:0;word-break:break-all}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input[type=text],input[type=password]{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--fg);font:inherit}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:12px}
button,.filebtn{font:inherit;padding:8px 14px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.danger{color:var(--bad);border-color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
.nets{list-style:none;margin:10px 0 0;padding:0;border:1px solid var(--line);border-radius:7px;max-height:220px;overflow:auto}
.nets:empty{display:none}
.nets li{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;border-bottom:1px solid var(--line);cursor:pointer}
.nets li:last-child{border-bottom:0}.nets li:hover{background:var(--bg)}
.nets span.s{color:var(--muted);font-size:13px;white-space:nowrap}
.msg{margin-top:10px;font-size:14px;min-height:1em}
.msg.ok{color:var(--ok)}.msg.bad{color:var(--bad)}
.bar{height:10px;border-radius:5px;background:var(--bg);border:1px solid var(--line);overflow:hidden;margin-top:12px;display:none}
.bar>div{height:100%;width:0;background:var(--accent);transition:width .2s}
.hint{font-size:13px;color:var(--muted);margin:6px 0 0}
input[type=file]{max-width:100%}
.chk{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted);margin-top:8px}
nav.tabs{display:flex;gap:4px;flex:0 0 100%}
nav.tabs a{padding:6px 12px;border-radius:7px;color:var(--muted);text-decoration:none;font-size:14px}
nav.tabs a.on{background:var(--bg);color:var(--fg);font-weight:600}
nav.tabs[hidden],section.card[hidden]{display:none}
</style>
</head>
<body>
<header><div class="wrap">
  <h1>%TITLE%</h1>
  <span class="pill" id="ver">v-</span>
  <span class="pill" id="mode">-</span>
  <nav class="tabs" id="tabs" hidden><a href="#home" data-tab="main">Home</a><a href="#settings" data-tab="settings">Settings</a></nav>
</div></header>
<main class="wrap">
%APP_HTML%

<section class="card" data-page="settings">
  <h2>Status</h2>
  <dl id="status"><dt>Loading</dt><dd>…</dd></dl>
</section>

<section class="card" data-page="settings">
  <h2>WiFi Setup</h2>
  <div class="row" style="margin-top:0">
    <button id="scanBtn" type="button">Scan for networks</button>
    <span class="hint" id="scanMsg"></span>
  </div>
  <ul class="nets" id="nets"></ul>
  <form id="wifiForm">
    <label for="ssid">Network name (SSID)</label>
    <input type="text" id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
    <label for="pass">Password</label>
    <input type="password" id="pass" name="pass" maxlength="63" autocomplete="off">
    <label class="chk"><input type="checkbox" id="showPass"> Show password</label>
    <div class="row"><button class="primary" type="submit">Save &amp; Connect</button></div>
    <div class="msg" id="wifiMsg"></div>
  </form>
</section>

<section class="card" data-page="settings">
  <h2>Firmware Update</h2>
  <p class="hint" style="margin:0 0 10px">Upload an %CHIP% application image (<code>firmware.bin</code> from <code>.pio/build/&lt;env&gt;/</code>). It must be built with the same partition table as this firmware. Do not power off while the upload runs.</p>
  <form id="fwForm">
    <input type="file" id="fwFile" accept=".bin,application/octet-stream" required>
    <div class="row"><button class="primary" type="submit" id="fwBtn">Upload &amp; Install</button></div>
    <div class="bar" id="fwBar"><div></div></div>
    <div class="msg" id="fwMsg"></div>
  </form>
</section>

<section class="card" data-page="settings">
  <h2>Device Settings</h2>
  <form id="devForm">
    <label for="hostname">Hostname (reachable as <i>hostname</i>.local)</label>
    <input type="text" id="hostname" name="hostname" maxlength="32" autocomplete="off">
    <label for="admin">New admin password (user: <b>admin</b>)</label>
    <input type="password" id="admin" name="admin" maxlength="64" autocomplete="new-password" placeholder="leave blank to keep current">
    <label class="chk"><input type="checkbox" id="adminClear" name="adminClear" value="1"> Remove admin password</label>
    <p class="hint" id="authHint"></p>
    <div class="row"><button class="primary" type="submit">Save</button></div>
    <div class="msg" id="devMsg"></div>
  </form>
</section>

<section class="card" data-page="settings">
  <h2>Maintenance</h2>
  <div class="row" style="margin-top:0">
    <button type="button" id="rebootBtn">Reboot</button>
    <button type="button" class="danger" id="forgetBtn">Forget WiFi</button>
  </div>
  <p class="hint">Holding the BOOT button for 5 seconds while running also erases the saved WiFi credentials.</p>
  <div class="msg" id="mntMsg"></div>
</section>

</main>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let rebooting = false;

// Pages: cards tagged data-page="settings" go on the Settings tab, all other
// cards on the main tab. Without app cards everything shows on one page.
let portal = false;
function showPage() {
  const cards = [...document.querySelectorAll('main > section.card')];
  const hasMain = cards.some(c => (c.dataset.page || 'main') === 'main');
  $('tabs').hidden = !hasMain;
  const page = !hasMain ? '' : location.hash === '#settings' || (portal && !location.hash) ? 'settings' : 'main';
  cards.forEach(c => { c.hidden = page && (c.dataset.page || 'main') !== page; });
  document.querySelectorAll('#tabs a').forEach(a => a.classList.toggle('on', a.dataset.tab === page));
}
window.addEventListener('hashchange', showPage);

function setMsg(el, text, ok) { el.textContent = text; el.className = 'msg' + (ok === undefined ? '' : ok ? ' ok' : ' bad'); }

function fmtBytes(n) { return n >= 1048576 ? (n / 1048576).toFixed(2) + ' MB' : (n / 1024).toFixed(1) + ' KB'; }
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + (s % 60) + 's';
}

async function loadStatus() {
  if (rebooting) return;
  try {
    const r = await fetch('/api/status', {cache: 'no-store'});
    if (!r.ok) return;
    const s = await r.json();
    $('ver').textContent = 'v' + s.version;
    if (portal !== (s.mode === 'portal')) { portal = s.mode === 'portal'; showPage(); }
    const m = $('mode');
    m.textContent = s.mode === 'portal' ? 'Setup AP' : (s.connected ? 'Connected' : 'Reconnecting');
    m.className = 'pill ' + (s.mode === 'portal' ? 'warn' : s.connected ? 'ok' : 'warn');
    const rows = [];
    if (s.mode === 'portal') {
      rows.push(['Setup AP', s.apSsid + ' (' + s.apIp + ')'], ['AP clients', s.apClients]);
      rows.push(['Saved network', s.savedSsid || '(none)']);
    } else {
      rows.push(['Network', s.savedSsid], ['IP address', s.ip || '-'], ['Signal', s.connected ? s.rssi + ' dBm' : '-']);
    }
    rows.push(['Hostname', s.hostname + '.local'], ['MAC', s.mac], ['Uptime', fmtUptime(s.uptime)],
      ['Firmware', 'v' + s.version + ' (' + s.build + ')'], ['Running slot', s.partition],
      ['App size / slot', fmtBytes(s.sketch) + ' / ' + fmtBytes(s.otaSpace)],
      ['Chip', s.chip + ', ' + fmtBytes(s.flash) + ' flash'], ['Free heap', fmtBytes(s.heap)], ['SDK', s.sdk]);
    $('status').innerHTML = rows.map(([k, v]) => '<dt>' + esc(k) + '</dt><dd>' + esc(v) + '</dd>').join('');
    if (document.activeElement !== $('hostname') && !$('hostname').dataset.touched) $('hostname').value = s.hostname;
    $('authHint').textContent = s.authEnabled ? 'An admin password is set; this page requires login.'
      : 'No admin password set: anyone on this network can change settings or upload firmware.';
    if (!$('ssid').value && s.savedSsid) $('ssid').value = s.savedSsid;
  } catch (e) {}
}

async function post(url, data) {
  const r = await fetch(url, {method: 'POST', body: new URLSearchParams(data)});
  let j; try { j = await r.json(); } catch (e) { j = {ok: false, message: 'HTTP ' + r.status}; }
  return j;
}

function waitForReboot(msgEl, text) {
  rebooting = true;
  setMsg(msgEl, text, true);
  let tries = 0;
  setTimeout(function poll() {
    tries++;
    fetch('/api/status', {cache: 'no-store'}).then(r => { if (r.ok) location.reload(); else throw 0; })
      .catch(() => {
        if (tries < 40) setTimeout(poll, 2000);
        else setMsg(msgEl, text + ' The device did not come back at this address. It may have joined your network; look for it at its hostname.local or in your router.', true);
      });
  }, 4000);
}

async function handleResult(j, msgEl) {
  setMsg(msgEl, j.message, j.ok);
  if (j.ok && j.restart) waitForReboot(msgEl, j.message);
}

// WiFi scan
$('scanBtn').onclick = async () => {
  const btn = $('scanBtn'); btn.disabled = true; $('scanMsg').textContent = 'Scanning…'; $('nets').innerHTML = '';
  let url = '/api/scan?fresh=1';
  for (let i = 0; i < 20; i++) {
    try {
      const j = await (await fetch(url, {cache: 'no-store'})).json();
      if (!j.scanning) {
        const nets = j.networks.sort((a, b) => b.rssi - a.rssi);
        $('nets').innerHTML = nets.map(n => '<li data-ssid="' + esc(n.ssid) + '"><span>' + (n.secure ? '&#128274; ' : '') + esc(n.ssid) +
          '</span><span class="s">' + n.rssi + ' dBm</span></li>').join('');
        $('scanMsg').textContent = nets.length ? nets.length + ' network(s) found. Tap one to select.' : 'No networks found.';
        btn.disabled = false; return;
      }
    } catch (e) {}
    url = '/api/scan';
    await new Promise(r => setTimeout(r, 1500));
  }
  $('scanMsg').textContent = 'Scan timed out.'; btn.disabled = false;
};
$('nets').onclick = e => {
  const li = e.target.closest('li'); if (!li) return;
  $('ssid').value = li.dataset.ssid; $('pass').value = ''; $('pass').focus();
};
$('showPass').onchange = e => { $('pass').type = e.target.checked ? 'text' : 'password'; };

$('wifiForm').onsubmit = async e => {
  e.preventDefault();
  setMsg($('wifiMsg'), 'Saving…');
  try { await handleResult(await post('/api/wifi', {ssid: $('ssid').value, pass: $('pass').value}), $('wifiMsg')); }
  catch (err) { setMsg($('wifiMsg'), 'Request failed.', false); }
};

// Device settings
$('hostname').oninput = () => { $('hostname').dataset.touched = '1'; };
$('devForm').onsubmit = async e => {
  e.preventDefault();
  const d = {hostname: $('hostname').value};
  if ($('adminClear').checked) d.adminClear = '1'; else if ($('admin').value) d.admin = $('admin').value;
  setMsg($('devMsg'), 'Saving…');
  try {
    const j = await post('/api/device', d);
    $('admin').value = ''; $('adminClear').checked = false; delete $('hostname').dataset.touched;
    await handleResult(j, $('devMsg'));
    if (j.ok && !j.restart) loadStatus();
  } catch (err) { setMsg($('devMsg'), 'Request failed.', false); }
};

// Firmware upload
$('fwForm').onsubmit = e => {
  e.preventDefault();
  const f = $('fwFile').files[0];
  if (!f) return;
  if (!/\.bin$/i.test(f.name) && !confirm('"' + f.name + '" does not end in .bin. Upload anyway?')) return;
  const fd = new FormData(); fd.append('firmware', f, f.name);
  const xhr = new XMLHttpRequest();
  const bar = $('fwBar'), fill = bar.firstElementChild;
  bar.style.display = 'block'; fill.style.width = '0'; $('fwBtn').disabled = true;
  setMsg($('fwMsg'), 'Uploading ' + f.name + ' (' + fmtBytes(f.size) + ')…');
  xhr.upload.onprogress = ev => {
    if (!ev.lengthComputable) return;
    const p = Math.round(ev.loaded / ev.total * 100);
    fill.style.width = p + '%';
    setMsg($('fwMsg'), p < 100 ? 'Uploading… ' + p + '%' : 'Finishing and verifying…');
  };
  xhr.onload = () => {
    let j; try { j = JSON.parse(xhr.responseText); } catch (err) { j = {ok: false, message: 'HTTP ' + xhr.status}; }
    $('fwBtn').disabled = false;
    if (!j.ok) bar.style.display = 'none';
    handleResult(j, $('fwMsg'));
  };
  xhr.onerror = () => { $('fwBtn').disabled = false; bar.style.display = 'none'; setMsg($('fwMsg'), 'Upload failed (connection lost).', false); };
  xhr.open('POST', '/update');
  xhr.send(fd);
};

// Maintenance
$('rebootBtn').onclick = async () => {
  if (!confirm('Reboot the device?')) return;
  try { await handleResult(await post('/api/reboot', {}), $('mntMsg')); } catch (e) { setMsg($('mntMsg'), 'Request failed.', false); }
};
$('forgetBtn').onclick = async () => {
  if (!confirm('Erase the saved WiFi credentials? The device will restart into setup AP mode.')) return;
  try { await handleResult(await post('/api/forget', {}), $('mntMsg')); } catch (e) { setMsg($('mntMsg'), 'Request failed.', false); }
};

showPage();
loadStatus();
setInterval(loadStatus, 5000);
</script>
<script>%APP_JS%</script>
</body>
</html>
)rawliteral";
