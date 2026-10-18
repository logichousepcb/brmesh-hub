#include "MeshHub.h"

#include <Preferences.h>
#include <esp_random.h>
#include <stdarg.h>

#include <deque>

#include "MeshRadio.h"

namespace MeshHub {

namespace {

const char* PREFS_NS = "brmesh";
constexpr uint8_t STORE_VERSION = 1;
constexpr size_t MAX_DISCOVERED = 32;
constexpr size_t MAX_HEARD = 32;
constexpr size_t LOG_LINES = 60;
constexpr uint32_t SCAN_TX_MS = 2500;
constexpr uint32_t BIND_TX_MS = 1500;
constexpr uint32_t SCAN_WINDOW_MS = 20000;   // UI shows "scanning" this long
constexpr uint32_t REBIND_GAP_MS = 3000;     // pending bind: resend at most this often
constexpr uint32_t MISMATCH_LOG_MS = 60000;  // "reports another address" log / auto rebind
constexpr uint32_t SAVE_DELAY_MS = 3000;     // debounce for state-only changes

struct __attribute__((packed)) StoredLight {
  char uid[17];
  char name[33];
  uint16_t type;
  uint8_t did[6];
  uint8_t key[4];
  uint8_t hasDid;
  uint8_t addr;
  uint8_t on, brightness, mode, r, g, b;
  uint16_t mireds;
};

enum JobKind : uint8_t { JOB_SCAN, JOB_BIND, JOB_LIGHT };
struct Job {
  JobKind kind;
  String uid;
};

Config g_cfg;
Settings g_set;
Listener g_listener;
Preferences g_prefs;
fastcon::Builder g_builder;

std::vector<Light> g_lights;
std::vector<Discovered> g_discovered;
std::vector<Heard> g_heard;
std::deque<Job> g_jobs;
std::deque<String> g_log;

bool g_dirty = false;
uint32_t g_dirtyAt = 0;
uint32_t g_scanUntil = 0;
uint32_t g_lastMismatchLog = 0;

// ------------------------------------------------------------- helpers --

bool parseHex(const String& s, uint8_t* out, size_t n) {
  if (s.length() != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    char buf[3] = {s[2 * i], s[2 * i + 1], 0};
    char* end;
    long v = strtol(buf, &end, 16);
    if (*end) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

String toHex(const uint8_t* d, size_t n) {
  String s;
  char b[3];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X", d[i]);
    s += b;
  }
  return s;
}

void copyStr(char* dst, size_t size, const String& src) {
  strncpy(dst, src.c_str(), size - 1);
  dst[size - 1] = 0;
}

uint8_t defaultMode(uint8_t caps) {
  if (caps & fastcon::CAP_RGB) return MODE_RGB;
  if (caps & fastcon::CAP_CT) return MODE_CT;
  if (caps & fastcon::CAP_WHITE) return MODE_WHITE;
  return (caps & fastcon::CAP_DIM) ? MODE_BRIGHTNESS : MODE_ONOFF;
}

bool modeSupported(uint8_t mode, uint8_t caps) {
  switch (mode) {
    case MODE_RGB: return caps & fastcon::CAP_RGB;
    case MODE_CT: return caps & fastcon::CAP_CT;
    case MODE_WHITE: return caps & fastcon::CAP_WHITE;
    case MODE_BRIGHTNESS: return caps & fastcon::CAP_DIM;
    case MODE_ONOFF: return caps == 0;
    default: return false;
  }
}

Light newLight(const String& name, uint16_t type, uint8_t addr) {
  Light l = {};
  copyStr(l.name, sizeof(l.name), name);
  l.type = type;
  l.addr = addr;
  l.brightness = 255;
  l.mode = defaultMode(fastcon::capabilities(type));
  l.r = l.g = l.b = 255;
  l.mireds = 300;
  return l;
}

Light* findByDid(const uint8_t did[6]) {
  for (auto& l : g_lights)
    if (l.hasDid && memcmp(l.did, did, 6) == 0) return &l;
  return nullptr;
}

Light* findByAddr(uint16_t addr) {
  for (auto& l : g_lights)
    if (l.addr == addr) return &l;
  return nullptr;
}

String shortName(const uint8_t did[6]) { return toHex(did + 4, 2); }

// --------------------------------------------------------------- storage --

void saveLights() {
  std::vector<StoredLight> out(g_lights.size());
  for (size_t i = 0; i < g_lights.size(); i++) {
    const Light& l = g_lights[i];
    StoredLight& s = out[i];
    memset(&s, 0, sizeof(s));
    memcpy(s.uid, l.uid, sizeof(s.uid));
    memcpy(s.name, l.name, sizeof(s.name));
    s.type = l.type;
    memcpy(s.did, l.did, 6);
    memcpy(s.key, l.key, 4);
    s.hasDid = l.hasDid;
    s.addr = l.addr;
    s.on = l.on;
    s.brightness = l.brightness;
    s.mode = l.mode;
    s.r = l.r;
    s.g = l.g;
    s.b = l.b;
    s.mireds = l.mireds;
  }
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putUChar("lver", STORE_VERSION);
  if (out.empty()) g_prefs.remove("lights");
  else g_prefs.putBytes("lights", out.data(), out.size() * sizeof(StoredLight));
  g_prefs.end();
  g_dirty = false;
}

void markDirty() {
  g_dirty = true;
  g_dirtyAt = millis();
}

void loadAll() {
  g_prefs.begin(PREFS_NS, true);
  String key = g_prefs.getString("key", g_cfg.defaultMeshKey);
  if (!parseHex(key, g_set.meshKey, 4)) parseHex("A1A2A3A4", g_set.meshKey, 4);
  g_set.txMs = g_prefs.getUShort("txms", 300);
  g_set.autoAdd = g_prefs.getBool("autoadd", false);
  g_set.mqttEnabled = g_prefs.getBool("mq_en", false);
  g_set.mqttHost = g_prefs.getString("mq_host", "");
  g_set.mqttPort = g_prefs.getUShort("mq_port", 1883);
  g_set.mqttUser = g_prefs.getString("mq_user", "");
  g_set.mqttPass = g_prefs.getString("mq_pass", "");
  g_set.mqttPrefix = g_prefs.getString("mq_pfx", "homeassistant");
  g_lights.clear();
  bool dropped = false;
  size_t size = g_prefs.getBytesLength("lights");
  if (g_prefs.getUChar("lver", 0) == STORE_VERSION && size && size % sizeof(StoredLight) == 0) {
    std::vector<StoredLight> in(size / sizeof(StoredLight));
    g_prefs.getBytes("lights", in.data(), size);
    for (const auto& s : in) {
      // Only discovered lights are kept; drop ones added by address.
      if (!s.hasDid) {
        dropped = true;
        continue;
      }
      Light l = {};
      memcpy(l.uid, s.uid, sizeof(l.uid));
      l.uid[sizeof(l.uid) - 1] = 0;
      memcpy(l.name, s.name, sizeof(l.name));
      l.name[sizeof(l.name) - 1] = 0;
      l.type = s.type;
      memcpy(l.did, s.did, 6);
      memcpy(l.key, s.key, 4);
      l.hasDid = s.hasDid;
      l.addr = s.addr;
      l.on = s.on;
      l.brightness = s.brightness ? s.brightness : 255;
      l.mode = s.mode;
      l.r = s.r;
      l.g = s.g;
      l.b = s.b;
      l.mireds = s.mireds;
      g_lights.push_back(l);
    }
  }
  g_prefs.end();

  if (dropped) saveLights();
}

// ------------------------------------------------------------ transmit --

void queueJob(JobKind kind, const String& uid = "") {
  for (const auto& j : g_jobs)
    if (j.kind == kind && j.uid == uid) return;  // already queued; built from latest state
  g_jobs.push_back({kind, uid});
}

size_t buildCommand(const Light& l, uint8_t cmd[6]) {
  uint8_t caps = fastcon::capabilities(l.type);
  if (!l.on) return fastcon::lightOff(cmd);
  uint8_t level = (uint8_t)max(1, ((int)l.brightness * 127 + 127) / 255);
  if (caps == 0) return fastcon::lightOn(cmd, 127);
  switch (l.mode) {
    case MODE_RGB:
      if (l.r || l.g || l.b) return fastcon::lightRgb(cmd, l.r, l.g, l.b, level);
      return fastcon::lightOn(cmd, level);
    case MODE_WHITE:
      return fastcon::lightWhite(cmd, level);
    case MODE_CT: {
      int m = constrain((int)l.mireds, (int)MIN_MIREDS, (int)MAX_MIREDS);
      uint8_t warm = (uint8_t)((m - MIN_MIREDS) * 255 / (MAX_MIREDS - MIN_MIREDS));
      return fastcon::lightColdWarm(cmd, warm, 255 - warm, level);
    }
    default:
      return fastcon::lightOn(cmd, level);
  }
}

void processQueue() {
  if (g_jobs.empty() || !MeshRadio::ready() || MeshRadio::busy()) return;
  Job job = g_jobs.front();
  g_jobs.pop_front();

  fastcon::Frame f;
  uint32_t duration;
  if (job.kind == JOB_SCAN) {
    f = g_builder.scan(false);
    duration = SCAN_TX_MS;
  } else {
    Light* l = find(job.uid);
    if (!l) return;
    if (job.kind == JOB_BIND) {
      f = g_builder.bind(l->did, l->addr, g_set.meshKey, l->key);
      duration = BIND_TX_MS;
      l->lastBind = millis();
    } else {
      uint8_t cmd[6];
      size_t n = buildCommand(*l, cmd);
      f = g_builder.light(l->addr, g_set.meshKey, cmd, n);
      duration = g_set.txMs;
    }
  }
  MeshRadio::broadcast(f, duration);
}

// ------------------------------------------------------------- receive --

void onDiscovery(const fastcon::Discovery& d, int8_t rssi) {
  uint32_t now = millis();
  if (Light* l = findByDid(d.did)) {
    l->lastHeard = now;
    if (l->type != d.type || memcmp(l->key, d.key, 4) != 0) {
      l->type = d.type;
      memcpy(l->key, d.key, 4);
      saveLights();
      if (g_listener.lightAdded) g_listener.lightAdded(*l);
    }
    if (l->bindPending) {
      if (now - l->lastBind >= REBIND_GAP_MS) {
        log("%s is in discovery mode, sending its bind (address %u)", l->name, l->addr);
        queueJob(JOB_BIND, l->uid);
      }
    } else if (d.reportedAddr != l->addr && now - g_lastMismatchLog >= MISMATCH_LOG_MS) {
      g_lastMismatchLog = now;
      if (g_set.autoAdd) {
        log("%s reports address %u, binding it back to %u", l->name, d.reportedAddr, l->addr);
        l->bindPending = true;
        queueJob(JOB_BIND, l->uid);
      } else {
        log("%s reports address %u but the hub uses %u: press Bind", l->name, d.reportedAddr,
            l->addr);
      }
    }
    return;
  }

  Discovered* found = nullptr;
  for (auto& x : g_discovered)
    if (memcmp(x.did, d.did, 6) == 0) found = &x;
  if (!found) {
    if (g_discovered.size() >= MAX_DISCOVERED) g_discovered.erase(g_discovered.begin());
    g_discovered.push_back({});
    found = &g_discovered.back();
    memcpy(found->did, d.did, 6);
    log("Found %s %s (address %u, %d dBm)", fastcon::typeName(d.type), shortName(d.did).c_str(),
        d.reportedAddr, rssi);
  }
  found->type = d.type;
  memcpy(found->key, d.key, 4);
  found->reportedAddr = d.reportedAddr;
  found->rssi = rssi;
  found->lastSeen = now;
  found->count++;

  if (g_set.autoAdd && fastcon::isLight(d.type)) {
    uint8_t addr = (d.reportedAddr && !addressUsed(d.reportedAddr)) ? d.reportedAddr
                                                                   : nextFreeAddress();
    if (addr) addDiscovered(hexDid(d.did), "", addr);
  }
}

void onHeartbeat(const fastcon::Heartbeat& hb) {
  uint32_t now = millis();
  if (hb.addr == 0) return;
  if (Light* l = findByAddr(hb.addr)) {
    l->lastHeard = now;
    if (l->bindPending) {
      l->bindPending = false;
      log("%s joined the mesh at address %u", l->name, l->addr);
    }
    return;
  }
  for (auto& h : g_heard) {
    if (h.addr == hb.addr) {
      h.lastSeen = now;
      h.count++;
      return;
    }
  }
  if (g_heard.size() >= MAX_HEARD) g_heard.erase(g_heard.begin());
  g_heard.push_back({hb.addr, now, 1});
  log("Heard a paired light at address %u that isn't in the list", hb.addr);
}

void processRx() {
  MeshRadio::RxPacket p;
  for (int i = 0; i < 16 && MeshRadio::receive(p); i++) {
    fastcon::Heartbeat hb;
    fastcon::Discovery d;
    if (fastcon::parseHeartbeat(p.data, p.len, g_set.meshKey, hb)) onHeartbeat(hb);
    else if (fastcon::parseDiscovery(p.data, p.len, d)) onDiscovery(d, p.rssi);
  }
}

}  // namespace

// ------------------------------------------------------------ public API --

void begin(const Config& cfg) {
  g_cfg = cfg;
  g_builder.setSequence((uint8_t)(esp_random() % 254 + 1));
  loadAll();
  log("%u light(s) loaded, mesh key %s", (unsigned)g_lights.size(),
      toHex(g_set.meshKey, 4).c_str());
}

void loop() {
  processRx();
  processQueue();
  if (g_dirty && millis() - g_dirtyAt >= SAVE_DELAY_MS) saveLights();
}

std::vector<Light>& lights() { return g_lights; }

Light* find(const String& uid) {
  for (auto& l : g_lights)
    if (uid == l.uid) return &l;
  return nullptr;
}

String hexDid(const uint8_t did[6]) { return toHex(did, 6); }

String topicName(const String& name) {
  String out;
  for (char ch : name) {
    char c = (char)tolower((unsigned char)ch);
    if (isalnum((unsigned char)c)) out += c;
    else if (out.length() && !out.endsWith("_")) out += '_';
  }
  while (out.endsWith("_")) out.remove(out.length() - 1);
  return out;
}

String topicName(const Light& l) {
  String t = topicName(String(l.name));
  if (t.isEmpty()) {
    t = l.uid;
    t.toLowerCase();
  }
  return t;
}

Light* findByTopic(const String& topic) {
  for (auto& l : g_lights)
    if (topicName(l) == topic) return &l;
  return nullptr;
}

// Checks a new name for a light (except: the light being renamed).
static String checkName(const String& name, const Light* except) {
  String t = topicName(name);
  if (t.isEmpty()) return "The name needs at least one letter or digit; it becomes the MQTT topic.";
  for (const auto& o : g_lights)
    if (&o != except && topicName(o) == t)
      return "\"" + String(o.name) + "\" already uses the MQTT name \"" + t + "\". Pick another name.";
  return "";
}

bool addressUsed(uint8_t addr, const Light* except) {
  for (const auto& l : g_lights)
    if (&l != except && l.addr == addr) return true;
  return false;
}

uint8_t nextFreeAddress() {
  for (int a = 1; a <= 255; a++) {
    bool heardOther = false;
    for (const auto& h : g_heard) heardOther |= h.addr == a;
    if (!addressUsed(a) && !heardOther) return a;
  }
  return 0;
}

String addDiscovered(const String& didHex, const String& name, uint8_t addr, Light** out) {
  uint8_t did[6];
  if (!parseHex(didHex, did, 6)) return "Invalid device ID.";
  auto it = g_discovered.begin();
  for (; it != g_discovered.end(); ++it)
    if (memcmp(it->did, did, 6) == 0) break;
  if (it == g_discovered.end()) return "That light is no longer in the discovered list. Scan again.";
  if (findByDid(did)) return "That light is already added.";
  if (g_lights.size() >= MAX_LIGHTS) return "The light list is full.";
  if (addr < 1) return "Mesh address must be 1-255.";
  if (addressUsed(addr)) return "Mesh address " + String(addr) + " is already used.";

  String nm = name;
  nm.trim();
  if (nm.isEmpty()) nm = "Light " + shortName(did);
  nm = nm.substring(0, sizeof(Light::name) - 1);
  String err = checkName(nm, nullptr);
  if (err.length()) return err;
  Light l = newLight(nm, it->type, addr);
  copyStr(l.uid, sizeof(l.uid), didHex);
  memcpy(l.did, did, 6);
  memcpy(l.key, it->key, 4);
  l.hasDid = true;
  l.bindPending = true;
  l.lastHeard = it->lastSeen;
  g_discovered.erase(it);
  for (auto h = g_heard.begin(); h != g_heard.end(); ++h) {
    if (h->addr == addr) {
      g_heard.erase(h);
      break;
    }
  }
  g_lights.push_back(l);
  saveLights();
  log("Added %s at address %u, binding", l.name, addr);
  queueJob(JOB_BIND, l.uid);
  if (g_listener.lightAdded) g_listener.lightAdded(g_lights.back());
  if (out) *out = &g_lights.back();
  return "";
}

String edit(Light& l, const String& name, int addr, int type) {
  bool changed = false;
  String nm = name;
  nm.trim();
  nm = nm.substring(0, sizeof(Light::name) - 1);
  if (nm.length() && nm != l.name) {
    String err = checkName(nm, &l);
    if (err.length()) return err;
  }
  if (addr >= 0 && addr != l.addr) {
    if (addr < 1 || addr > 255) return "Mesh address must be 1-255.";
    if (addressUsed((uint8_t)addr, &l)) return "Mesh address " + String(addr) + " is already used.";
  }
  if (nm.length() && nm != l.name) {
    copyStr(l.name, sizeof(l.name), nm);
    changed = true;
  }
  if (type >= 0 && !l.hasDid && type != l.type) {
    l.type = (uint16_t)type;
    if (!modeSupported(l.mode, fastcon::capabilities(l.type)))
      l.mode = defaultMode(fastcon::capabilities(l.type));
    changed = true;
  }
  String msg;
  if (addr >= 0 && addr != l.addr) {
    if (addr < 1 || addr > 255) return "Mesh address must be 1-255.";
    if (addressUsed((uint8_t)addr, &l)) return "Mesh address " + String(addr) + " is already used.";
    l.addr = (uint8_t)addr;
    changed = true;
    if (l.hasDid) {
      l.bindPending = true;
      queueJob(JOB_BIND, l.uid);
      log("%s moved to address %u, binding", l.name, l.addr);
    }
  }
  if (changed) {
    saveLights();
    if (g_listener.lightAdded) g_listener.lightAdded(l);
  }
  return "";
}

String bind(Light& l) {
  if (!l.hasDid) return "This light was added by address, so the hub doesn't know its device ID. "
                        "Power-cycle it and scan to discover it instead.";
  l.bindPending = true;
  queueJob(JOB_BIND, l.uid);
  log("Binding %s to address %u", l.name, l.addr);
  return "";
}

void remove(const String& uid) {
  for (auto it = g_lights.begin(); it != g_lights.end(); ++it) {
    if (uid == it->uid) {
      log("Removed %s", it->name);
      g_lights.erase(it);
      saveLights();
      for (auto j = g_jobs.begin(); j != g_jobs.end();) j = (j->uid == uid) ? g_jobs.erase(j) : j + 1;
      if (g_listener.lightRemoved) g_listener.lightRemoved(uid);
      return;
    }
  }
}

uint8_t supportedCaps(const Light& l) { return fastcon::capabilities(l.type); }

void control(Light& l, const Control& c) {
  uint8_t caps = fastcon::capabilities(l.type);
  bool turnOn = false;
  if (c.brightness == 0) {
    l.on = false;
  } else if (c.brightness > 0 && (caps & fastcon::CAP_DIM)) {
    l.brightness = (uint8_t)min((int)c.brightness, 255);
    turnOn = true;
  }
  if (c.r >= 0 && c.g >= 0 && c.b >= 0 && (caps & fastcon::CAP_RGB)) {
    l.r = (uint8_t)min((int)c.r, 255);
    l.g = (uint8_t)min((int)c.g, 255);
    l.b = (uint8_t)min((int)c.b, 255);
    l.mode = MODE_RGB;
    turnOn = true;
  }
  if (c.mireds >= 0 && (caps & fastcon::CAP_CT)) {
    l.mireds = (uint16_t)constrain((int)c.mireds, (int)MIN_MIREDS, (int)MAX_MIREDS);
    l.mode = MODE_CT;
    turnOn = true;
  }
  if (c.mode >= 0 && modeSupported((uint8_t)c.mode, caps)) {
    l.mode = (uint8_t)c.mode;
    turnOn = true;
  }
  if (!modeSupported(l.mode, caps)) l.mode = defaultMode(caps);
  if (c.on >= 0) l.on = c.on;
  else if (turnOn) l.on = true;

  queueJob(JOB_LIGHT, l.uid);
  markDirty();
  if (g_listener.stateChanged) g_listener.stateChanged(l);
}

void allOnOff(bool on) {
  Control c;
  c.on = on ? 1 : 0;
  for (auto& l : g_lights) control(l, c);
}

void scan() {
  queueJob(JOB_SCAN);
  g_scanUntil = millis() + SCAN_WINDOW_MS;
  log("Scanning for lights in pairing mode");
}

bool scanning() { return (int32_t)(g_scanUntil - millis()) > 0; }

const std::vector<Discovered>& discovered() { return g_discovered; }
const std::vector<Heard>& heard() { return g_heard; }

const Settings& settings() { return g_set; }

String setMeshKey(const String& hex) {
  uint8_t k[4];
  String h = hex;
  h.trim();
  h.toUpperCase();
  if (h.startsWith("0X")) h = h.substring(2);
  if (!parseHex(h, k, 4)) return "Mesh key must be 8 hex characters, e.g. 32323035.";
  if (memcmp(k, g_set.meshKey, 4) == 0) return "";
  memcpy(g_set.meshKey, k, 4);
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putString("key", h);
  g_prefs.end();
  g_heard.clear();
  log("Mesh key set to %s", h.c_str());
  return "";
}

void setTxMs(uint16_t ms) {
  ms = constrain(ms, 50, 3000);
  if (ms == g_set.txMs) return;
  g_set.txMs = ms;
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putUShort("txms", ms);
  g_prefs.end();
}

void setAutoAdd(bool on) {
  if (on == g_set.autoAdd) return;
  g_set.autoAdd = on;
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putBool("autoadd", on);
  g_prefs.end();
  log("Auto-add %s", on ? "on" : "off");
}

void setMqtt(bool enabled, const String& host, uint16_t port, const String& user,
             const String& pass, const String& prefix) {
  g_set.mqttEnabled = enabled;
  g_set.mqttHost = host;
  g_set.mqttPort = port ? port : 1883;
  g_set.mqttUser = user;
  g_set.mqttPass = pass;
  g_set.mqttPrefix = prefix.length() ? prefix : "homeassistant";
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putBool("mq_en", g_set.mqttEnabled);
  g_prefs.putString("mq_host", g_set.mqttHost);
  g_prefs.putUShort("mq_port", g_set.mqttPort);
  g_prefs.putString("mq_user", g_set.mqttUser);
  g_prefs.putString("mq_pass", g_set.mqttPass);
  g_prefs.putString("mq_pfx", g_set.mqttPrefix);
  g_prefs.end();
  if (g_listener.settingsChanged) g_listener.settingsChanged();
}

void log(const char* fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  uint32_t s = millis() / 1000;
  char line[180];
  snprintf(line, sizeof(line), "%02lu:%02lu:%02lu %s", (unsigned long)(s / 3600),
           (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), msg);
  Serial.printf("[mesh] %s\n", msg);
  if (g_log.size() >= LOG_LINES) g_log.pop_front();
  g_log.push_back(line);
}

std::vector<String> recentLog() { return std::vector<String>(g_log.begin(), g_log.end()); }

size_t queueLength() { return g_jobs.size(); }

void setListener(const Listener& l) { g_listener = l; }

}  // namespace MeshHub
