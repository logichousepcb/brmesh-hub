#pragma once

// BRMesh cards for the BaseCore page (BaseConfig::appHtml / appScript). The
// script runs after the base script and uses its helpers ($, esc, post,
// setMsg, handleResult). Cards tagged data-page="settings" go on the
// Settings tab; the rest (lights, adding lights) on the main tab.

static const char MESH_HTML[] = R"rawliteral(
<style>
.h2row{display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-bottom:12px}
.h2row h2{margin:0}
button.sm{padding:5px 10px;font-size:13px}
button.act{border-color:var(--accent);color:var(--accent)}
input[type=number],select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--fg);font:inherit}
input[type=color]{width:46px;height:32px;border:1px solid var(--line);border-radius:6px;background:none;padding:2px;cursor:pointer}
.empty{color:var(--muted);font-size:14px;margin:4px 0}
.lt{border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-top:10px}
.lt:first-child{margin-top:0}
.lt-head{display:flex;align-items:center;gap:12px}
.lt-title{flex:1;min-width:0}
.lt-title b{word-break:break-word}
.lt-meta{font-size:13px;color:var(--muted)}
.topic{font-family:ui-monospace,Consolas,monospace;font-size:12px;color:var(--muted);word-break:break-all}
.lt-ctl{display:flex;flex-wrap:wrap;gap:8px 16px;align-items:center;margin-top:8px}
.lt-lab{display:flex;align-items:center;gap:8px;margin:0;font-size:13px;color:var(--muted);flex:1 1 180px}
.lt-lab.fit{flex:0 0 auto}
.lt-lab input[type=range]{flex:1;min-width:0}
.lt-form{border-top:1px solid var(--line);margin-top:10px;padding-top:4px}
.warn{color:var(--warn)}.okc{color:var(--ok)}
.sw{position:relative;display:inline-block;width:46px;height:26px;margin:0;flex:0 0 auto}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:var(--line);border-radius:13px;cursor:pointer;transition:background .15s}
.sw span:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:transform .15s}
.sw input:checked+span{background:var(--accent)}
.sw input:checked+span:before{transform:translateX(20px)}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
@media (max-width:520px){.grid2{grid-template-columns:1fr}}
.item{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:10px 0;border-top:1px solid var(--line)}
.item:first-child{border-top:0}
.item .info{flex:1 1 200px;min-width:0}
.item .sub{font-size:13px;color:var(--muted)}
.item input[type=text]{flex:1 1 140px;width:auto}
.item input[type=number]{flex:0 0 76px;width:76px}
h3{font-size:14px;margin:16px 0 4px}
ol.steps{margin:0 0 4px;padding-left:20px;font-size:14px}
ol.steps li{margin:2px 0}
pre.log{max-height:260px;overflow:auto;font-size:12px;line-height:1.5;background:var(--bg);border:1px solid var(--line);border-radius:7px;padding:8px 10px;margin:0;white-space:pre-wrap;word-break:break-word}
details summary{cursor:pointer;font-size:14px;color:var(--muted);margin-top:14px}
</style>

<section class="card">
  <div class="h2row">
    <h2>Lights</h2>
    <div class="row" style="margin:0">
      <button type="button" class="sm" id="allOn">All on</button>
      <button type="button" class="sm" id="allOff">All off</button>
    </div>
  </div>
  <div id="ltList"><p class="empty">Loading…</p></div>
  <div class="msg" id="ltMsg"></div>
</section>

<section class="card">
  <h2>Add Lights</h2>
  <ol class="steps">
    <li>Switch the light off at the wall for 5 seconds, then back on. That puts it in pairing mode.</li>
    <li>Press <b>Scan for lights</b>. Lights in pairing mode appear below within a few seconds.</li>
    <li>Give each one a friendly name and press <b>Add</b>. The hub binds it to its mesh address with your mesh key. The name is also the light's MQTT topic.</li>
  </ol>
  <div class="row">
    <button type="button" class="primary" id="scanLights">Scan for lights</button>
    <span class="pill warn" id="scanState" hidden>Scanning…</span>
  </div>
  <label class="chk"><input type="checkbox" id="autoAdd"> Add and bind every light a scan finds</label>

  <h3>Found by scan</h3>
  <div id="discList"><p class="empty">Nothing yet.</p></div>
  <div class="msg" id="addMsg"></div>
</section>

<section class="card" data-page="settings">
  <h2>Mesh &amp; Home Assistant</h2>
  <form id="meshForm" class="grid2">
    <div><label for="meshKey">Mesh key (8 hex characters)</label><input type="text" id="meshKey" maxlength="8" autocomplete="off" spellcheck="false"></div>
    <div><label for="txMs">Broadcast time per command (ms)</label><input type="number" id="txMs" min="50" max="3000" step="50"></div>
    <div><button class="primary" type="submit" style="margin-top:12px">Save mesh settings</button></div>
  </form>
  <p class="hint">Lights bound by this hub use this key. To control lights paired with the BRMesh app, enter the app's key. Changing it doesn't re-key lights that are already bound.</p>
  <div class="msg" id="meshMsg"></div>

  <h3>MQTT (Home Assistant discovery) <span class="pill" id="mqState">off</span></h3>
  <form id="mqForm" class="grid2">
    <label class="chk" style="grid-column:1/-1"><input type="checkbox" id="mqEn"> Connect to an MQTT broker</label>
    <div><label for="mqHost">Broker address</label><input type="text" id="mqHost" maxlength="64" placeholder="homeassistant.local" autocomplete="off"></div>
    <div><label for="mqPort">Port</label><input type="number" id="mqPort" min="1" max="65535" placeholder="1883"></div>
    <div><label for="mqUser">Username</label><input type="text" id="mqUser" maxlength="64" autocomplete="off"></div>
    <div><label for="mqPass">Password</label><input type="password" id="mqPass" maxlength="64" autocomplete="new-password"></div>
    <div><label for="mqPfx">Discovery prefix</label><input type="text" id="mqPfx" maxlength="32" placeholder="homeassistant"></div>
    <div><button class="primary" type="submit" style="margin-top:12px">Save MQTT</button></div>
  </form>
  <p class="hint" id="mqHint">Every light appears in Home Assistant through MQTT discovery, along with a "Scan for lights" button.</p>
  <div class="msg" id="mqMsg"></div>
</section>

<section class="card" data-page="settings">
  <h2>Activity</h2>
  <pre class="log" id="meshLog">…</pre>
  <p class="hint" id="radioStats"></p>
</section>
)rawliteral";

static const char MESH_JS[] = R"rawliteral(
const CAP = {DIM: 1, RGB: 2, WHITE: 4, CT: 8};
const MODE = {ONOFF: 0, BRI: 1, RGB: 2, WHITE: 3, CT: 4};
const mesh = {busy: {}, timers: {}, latest: {}, types: null, filled: false, node: ''};

// Same rule as MeshHub::topicName: lower case, other characters -> "_".
function topicName(name) { return name.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, ''); }
function topicFor(name) { const t = topicName(name); return t ? 'brmesh/' + mesh.node + '/light/' + t : ''; }
function showTopic(el, name) {
  const t = topicFor(name);
  el.textContent = t ? 'MQTT topic: ' + t + '/set' : 'The name needs a letter or digit for the MQTT topic.';
}

function ago(s) {
  if (s < 0) return 'never';
  if (s < 60) return s + ' s ago';
  if (s < 3600) return Math.floor(s / 60) + ' min ago';
  return Math.floor(s / 3600) + ' h ago';
}
function markBusy(uid) { mesh.busy[uid] = Date.now() + 2500; }
function isBusy(uid) { return (mesh.busy[uid] || 0) > Date.now(); }

// Light commands: at most one request per light every 250 ms, last value wins.
function sendControl(uid, data) {
  markBusy(uid);
  mesh.latest[uid] = Object.assign(mesh.latest[uid] || {}, data);
  if (mesh.timers[uid]) return;
  const flush = async () => {
    const d = mesh.latest[uid]; delete mesh.latest[uid];
    if (!d) { delete mesh.timers[uid]; return; }
    try {
      const j = await post('/api/mesh/control', Object.assign({uid}, d));
      if (!j.ok) setMsg($('ltMsg'), j.message, false);
    } catch (e) { setMsg($('ltMsg'), 'Request failed.', false); }
    mesh.timers[uid] = setTimeout(flush, 250);
  };
  mesh.timers[uid] = setTimeout(flush, 0);
}

function typeOptions(sel) {
  return (mesh.types || []).map(([v, n]) => '<option value="' + v + '"' + (v == sel ? ' selected' : '') + '>' + esc(n) + '</option>').join('');
}

function lightSig(l) { return [l.name, l.addr, l.type, l.caps, l.hasDid, l.topic].join('|'); }

function lightHtml(l) {
  const c = l.caps;
  let h = '<div class="lt-head"><div class="lt-title"><b>' + esc(l.name) + '</b><div class="lt-meta"></div>' +
    '<div class="topic">' + esc(l.topic) + '</div></div>' +
    '<label class="sw" title="On / off"><input type="checkbox" class="lt-on"><span></span></label></div><div class="lt-ctl">';
  if (c & CAP.DIM) h += '<label class="lt-lab">Brightness <input type="range" class="lt-bri" min="1" max="255"></label>';
  if (c & CAP.CT) h += '<label class="lt-lab">Cool <input type="range" class="lt-ct" min="153" max="500"> Warm</label>';
  if (c & CAP.RGB) h += '<label class="lt-lab fit">Colour <input type="color" class="lt-rgb"></label>';
  if (c & CAP.WHITE) h += '<button type="button" class="sm lt-white">White</button>';
  h += '<button type="button" class="sm lt-edit">Edit</button></div>' +
    '<form class="lt-form" hidden><div class="grid2">' +
    '<div><label>Friendly name</label><input type="text" name="name" class="nm" maxlength="32" value="' + esc(l.name) + '">' +
    '<div class="topic tp"></div></div>' +
    '<div><label>Mesh address</label><input type="number" name="addr" min="1" max="255" value="' + l.addr + '"></div>' +
    (l.hasDid ? '' : '<div><label>Light type</label><select name="type">' + typeOptions(l.type) + '</select></div>') +
    '</div><div class="row">' +
    '<button type="submit" class="primary sm">Save</button>' +
    (l.hasDid ? '<button type="button" class="sm lt-bind">Bind again</button>' : '') +
    '<button type="button" class="sm danger lt-del">Remove</button></div>' +
    '<p class="hint">' + (l.hasDid ? 'Device ID ' + esc(l.did) + '. Changing the address re-binds the light; if it doesn\'t take it, switch the light off and on.'
      : 'Added by address: the hub sends to address ' + l.addr + ' but can\'t re-bind it.') + '</p></form>';
  return h;
}

function updateLight(el, l) {
  const meta = el.querySelector('.lt-meta');
  const status = l.pending ? '<span class="warn">waiting for the light to confirm the bind</span>'
    : l.heard >= 0 ? (l.heard < 900 ? '<span class="okc">heard ' + ago(l.heard) + '</span>' : 'heard ' + ago(l.heard)) : 'not heard yet';
  meta.innerHTML = 'Address ' + l.addr + ' · ' + esc(l.typeName) + ' · ' + status;
  if (isBusy(l.uid) || el.contains(document.activeElement) && document.activeElement.tagName !== 'BUTTON') return;
  el.querySelector('.lt-on').checked = l.on;
  const bri = el.querySelector('.lt-bri'); if (bri) bri.value = l.bri;
  const ct = el.querySelector('.lt-ct'); if (ct) ct.value = l.mireds;
  const rgb = el.querySelector('.lt-rgb'); if (rgb) rgb.value = '#' + l.rgb;
  const w = el.querySelector('.lt-white'); if (w) w.classList.toggle('act', l.mode === MODE.WHITE);
}

function renderLights(d) {
  const list = $('ltList');
  if (!d.lights.length) { list.innerHTML = '<p class="empty">No lights yet. Scan for them below.</p>'; return; }
  list.querySelector('.empty')?.remove();
  const keep = new Set();
  d.lights.forEach((l, i) => {
    keep.add(l.uid);
    let el = list.querySelector('.lt[data-uid="' + CSS.escape(l.uid) + '"]');
    if (el && el.dataset.sig !== lightSig(l) && el.querySelector('.lt-form').hidden) { el.remove(); el = null; }
    if (!el) {
      el = document.createElement('div');
      el.className = 'lt'; el.dataset.uid = l.uid; el.dataset.sig = lightSig(l);
      el.innerHTML = lightHtml(l);
    }
    if (list.children[i] !== el) list.insertBefore(el, list.children[i] || null);
    updateLight(el, l);
  });
  [...list.querySelectorAll('.lt')].forEach(el => { if (!keep.has(el.dataset.uid)) el.remove(); });
}

function keyedList(box, items, key, make, update, emptyText) {
  if (!items.length) { box.innerHTML = '<p class="empty">' + emptyText + '</p>'; return; }
  box.querySelector('.empty')?.remove();
  const keep = new Set();
  items.forEach((it, i) => {
    const k = String(key(it)); keep.add(k);
    let el = box.querySelector('[data-k="' + CSS.escape(k) + '"]');
    if (!el) { el = document.createElement('div'); el.className = 'item'; el.dataset.k = k; el.innerHTML = make(it); }
    if (box.children[i] !== el) box.insertBefore(el, box.children[i] || null);
    update(el, it);
  });
  [...box.querySelectorAll('.item')].forEach(el => { if (!keep.has(el.dataset.k)) el.remove(); });
}

function renderDiscovery(d) {
  $('scanState').hidden = !d.scanning;
  keyedList($('discList'), d.discovered, x => x.did,
    x => '<div class="info"><b>' + esc(x.typeName) + ' ' + esc(x.short) + '</b><div class="sub"></div>' +
      (x.isLight ? '<div class="topic tp"></div>' : '') + '</div>' +
      (x.isLight ? '<input type="text" class="dn nm" maxlength="32" placeholder="Friendly name, e.g. Porch Light" data-def="Light ' + esc(x.short) + '">' +
        '<input type="number" class="da" min="1" max="255" title="Mesh address" value="' + x.suggest + '">' +
        '<button type="button" class="primary sm d-add">Add</button>' : '<span class="sub">not a light</span>'),
    (el, x) => {
      const dn = el.querySelector('.dn');
      if (dn && !el.querySelector('.tp').textContent) showTopic(el.querySelector('.tp'), dn.value || dn.dataset.def);
      el.querySelector('.sub').textContent = 'ID ' + x.did + ' · ' + x.rssi + ' dBm · ' +
        (x.addr ? 'currently address ' + x.addr : 'not bound') + ' · seen ' + ago(x.ago);
    }, 'Nothing yet. Power-cycle a light and press Scan.');
}

function fillOnce(id, v) { const el = $(id); if (!el.dataset.touched && document.activeElement !== el) el.value = v; }

function renderSettings(d) {
  if (!mesh.types) mesh.types = d.types;
  mesh.node = d.mqtt.node;
  fillOnce('meshKey', d.meshKey);
  fillOnce('txMs', d.txMs);
  $('autoAdd').checked = d.autoAdd;
  const m = d.mqtt;
  if (!mesh.filled) {
    mesh.filled = true;
    $('mqEn').checked = m.enabled; $('mqHost').value = m.host; $('mqPort').value = m.port;
    $('mqUser').value = m.user; $('mqPfx').value = m.prefix;
    $('mqPass').placeholder = m.passSet ? '(unchanged)' : '';
  }
  const st = $('mqState');
  st.textContent = !m.enabled ? 'off' : m.connected ? 'connected' : 'not connected';
  st.className = 'pill ' + (!m.enabled ? '' : m.connected ? 'ok' : 'warn');
  $('mqHint').textContent = 'Every light appears in Home Assistant through MQTT discovery, along with a "Scan for lights" button. Topics: brmesh/' + m.node + '/…';
}

function renderLog(d) {
  const box = $('meshLog');
  const atEnd = box.scrollTop + box.clientHeight >= box.scrollHeight - 4;
  box.textContent = d.log.join('\n') || 'No activity yet.';
  if (atEnd) box.scrollTop = box.scrollHeight;
  const r = d.radio;
  $('radioStats').textContent = (r.ready ? 'Bluetooth ready' : 'Bluetooth starting…') + ' · ' + r.rx + ' BRMesh frames received' +
    (r.dropped ? ' (' + r.dropped + ' dropped)' : '') + ' · ' + r.tx + ' sent · ' + r.queue + ' queued';
}

async function meshPoll() {
  try {
    const r = await fetch('/api/mesh', {cache: 'no-store'});
    if (!r.ok) return;
    const d = await r.json();
    renderSettings(d); renderLights(d); renderDiscovery(d); renderLog(d);
  } catch (e) {}
}

async function meshPost(url, data, msgEl) {
  try {
    const j = await post(url, data);
    setMsg(msgEl, j.message, j.ok);
    meshPoll();
    return j;
  } catch (e) { setMsg(msgEl, 'Request failed.', false); return {ok: false}; }
}

// Topic preview while typing a name
document.addEventListener('input', e => {
  if (!e.target.classList.contains('nm')) return;
  const tp = e.target.closest('.item, .lt').querySelector('.tp');
  if (tp) showTopic(tp, e.target.value || e.target.dataset.def || '');
});

// Light list events
const ltList = $('ltList');
ltList.addEventListener('input', e => {
  const el = e.target.closest('.lt'); if (!el) return;
  const uid = el.dataset.uid, t = e.target;
  if (t.classList.contains('lt-bri')) sendControl(uid, {bri: t.value});
  else if (t.classList.contains('lt-ct')) sendControl(uid, {mireds: t.value});
  else if (t.classList.contains('lt-rgb')) sendControl(uid, {rgb: t.value.slice(1)});
});
ltList.addEventListener('change', e => {
  const el = e.target.closest('.lt'); if (!el) return;
  if (e.target.classList.contains('lt-on')) sendControl(el.dataset.uid, {on: e.target.checked ? 1 : 0});
});
ltList.addEventListener('click', async e => {
  const el = e.target.closest('.lt'); if (!el) return;
  const uid = el.dataset.uid, t = e.target, name = el.querySelector('.lt-title b').textContent;
  if (t.classList.contains('lt-white')) sendControl(uid, {mode: 'white'});
  else if (t.classList.contains('lt-edit')) {
    const f = el.querySelector('.lt-form'); f.hidden = !f.hidden;
    showTopic(f.querySelector('.tp'), f.querySelector('.nm').value);
  }
  else if (t.classList.contains('lt-bind')) meshPost('/api/mesh/bind', {uid}, $('ltMsg'));
  else if (t.classList.contains('lt-del')) {
    if (confirm('Remove "' + name + '" from the hub? The light keeps its mesh address and key.')) meshPost('/api/mesh/remove', {uid}, $('ltMsg'));
  }
});
ltList.addEventListener('submit', async e => {
  e.preventDefault();
  const el = e.target.closest('.lt'), f = e.target, field = n => f.querySelector('[name="' + n + '"]');
  const d = {uid: el.dataset.uid, name: field('name').value, addr: field('addr').value};
  if (field('type')) d.type = field('type').value;
  const j = await meshPost('/api/mesh/edit', d, $('ltMsg'));
  if (j.ok) f.hidden = true;
});
$('allOn').onclick = () => meshPost('/api/mesh/all', {on: 1}, $('ltMsg'));
$('allOff').onclick = () => meshPost('/api/mesh/all', {on: 0}, $('ltMsg'));

// Discovery
$('scanLights').onclick = () => meshPost('/api/mesh/scan', {}, $('addMsg'));
$('autoAdd').onchange = e => meshPost('/api/mesh/settings', {autoadd: e.target.checked ? 1 : 0}, $('addMsg'));
$('discList').addEventListener('click', e => {
  if (!e.target.classList.contains('d-add')) return;
  const it = e.target.closest('.item');
  meshPost('/api/mesh/add', {did: it.dataset.k, name: it.querySelector('.dn').value, addr: it.querySelector('.da').value}, $('addMsg'));
});
['meshKey', 'txMs'].forEach(id => $(id).addEventListener('input', () => { $(id).dataset.touched = '1'; }));

// Settings
$('meshForm').onsubmit = async e => {
  e.preventDefault();
  const j = await meshPost('/api/mesh/settings', {key: $('meshKey').value, txms: $('txMs').value}, $('meshMsg'));
  if (j.ok) { delete $('meshKey').dataset.touched; delete $('txMs').dataset.touched; }
};
$('mqForm').onsubmit = async e => {
  e.preventDefault();
  const d = {enabled: $('mqEn').checked ? 1 : 0, host: $('mqHost').value, port: $('mqPort').value || 1883,
    user: $('mqUser').value, prefix: $('mqPfx').value};
  if ($('mqPass').value) d.pass = $('mqPass').value;
  const j = await meshPost('/api/mesh/mqtt', d, $('mqMsg'));
  if (j.ok) { $('mqPass').value = ''; mesh.filled = false; }
};

meshPoll();
setInterval(meshPoll, 2000);
)rawliteral";
