#include "MeshApi.h"

#include <ArduinoJson.h>
#include <BaseCore.h>
#include <uri/UriBraces.h>

#include "MeshHub.h"
#include "MeshMqtt.h"
#include "MeshRadio.h"

namespace MeshApi {

namespace {

const char* g_version = "";

WebServer& srv() { return BaseCore::server(); }

void sendDoc(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  srv().sendHeader("Cache-Control", "no-store");
  srv().send(code, "application/json", out);
}

void sendError(int code, const String& message) {
  JsonDocument doc;
  doc["error"] = message;
  sendDoc(code, doc);
}

// Parses the JSON request body into doc. An empty body is an empty object.
// Sends 400 and returns false when the body isn't a JSON object.
bool readBody(JsonDocument& doc) {
  String body = srv().arg("plain");
  body.trim();
  if (body.isEmpty()) {
    doc.to<JsonObject>();
    return true;
  }
  DeserializationError err = deserializeJson(doc, body);
  if (err || !doc.is<JsonObject>()) {
    sendError(400, err ? String("Invalid JSON: ") + err.c_str() : "The body must be a JSON object.");
    return false;
  }
  return true;
}

int32_t secondsAgo(uint32_t t) { return t ? (int32_t)((millis() - t) / 1000) : -1; }

const char* modeName(uint8_t mode) {
  switch (mode) {
    case MeshHub::MODE_BRIGHTNESS: return "brightness";
    case MeshHub::MODE_RGB: return "rgb";
    case MeshHub::MODE_WHITE: return "white";
    case MeshHub::MODE_CT: return "color_temp";
    default: return "onoff";
  }
}

void addCaps(JsonArray a, uint8_t caps) {
  if (caps & fastcon::CAP_DIM) a.add("brightness");
  if (caps & fastcon::CAP_RGB) a.add("rgb");
  if (caps & fastcon::CAP_WHITE) a.add("white");
  if (caps & fastcon::CAP_CT) a.add("color_temp");
}

void stateJson(JsonObject o, const MeshHub::Light& l) {
  o["on"] = l.on;
  o["brightness"] = l.brightness;
  o["color_mode"] = modeName(l.mode);
  JsonArray rgb = o["rgb"].to<JsonArray>();
  rgb.add(l.r);
  rgb.add(l.g);
  rgb.add(l.b);
  o["color_temp"] = l.mireds;
  o["color_temp_kelvin"] = l.mireds ? 1000000UL / l.mireds : 0;
}

void lightJson(JsonObject o, const MeshHub::Light& l) {
  o["id"] = l.uid;
  o["name"] = l.name;
  o["address"] = l.addr;
  o["type"] = l.type;
  o["type_name"] = fastcon::typeName(l.type);
  o["device_id"] = MeshHub::hexDid(l.did);
  addCaps(o["capabilities"].to<JsonArray>(), MeshHub::supportedCaps(l));
  stateJson(o["state"].to<JsonObject>(), l);
  int32_t heard = secondsAgo(l.lastHeard);
  if (heard < 0) o["last_heard_s"] = nullptr;
  else o["last_heard_s"] = heard;
  o["bind_pending"] = l.bindPending;
  o["mqtt_topic"] = MeshMqtt::lightTopic(l);
}

void sendLight(int code, const MeshHub::Light& l) {
  JsonDocument doc;
  lightJson(doc.to<JsonObject>(), l);
  sendDoc(code, doc);
}

// Path IDs may be the light's id, its name (case-insensitive, URL-encoded),
// or its mesh address.
MeshHub::Light* findLight(const String& raw) {
  String id = WebServer::urlDecode(raw);
  if (MeshHub::Light* l = MeshHub::find(id)) return l;
  for (auto& l : MeshHub::lights())
    if (id.equalsIgnoreCase(l.name)) return &l;
  bool digits = id.length() > 0 && id.length() <= 3;
  for (char c : id) digits &= isdigit((unsigned char)c) != 0;
  if (digits) {
    int addr = id.toInt();
    for (auto& l : MeshHub::lights())
      if (l.addr == addr) return &l;
  }
  return nullptr;
}

MeshHub::Light* requireLight() {
  MeshHub::Light* l = findLight(srv().pathArg(0));
  if (!l) sendError(404, "No light with id, name or address \"" + WebServer::urlDecode(srv().pathArg(0)) + "\".");
  return l;
}

bool hexColor(const char* s, MeshHub::Control& c) {
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  for (const char* p = s; *p; p++)
    if (!isxdigit((unsigned char)*p)) return false;
  long v = strtol(s, nullptr, 16);
  c.r = (v >> 16) & 0xFF;
  c.g = (v >> 8) & 0xFF;
  c.b = v & 0xFF;
  return true;
}

// Builds a Control from a state body. toggleFrom: the light whose "on" a
// "toggle" flips (nullptr for all lights, where toggle isn't allowed).
bool parseState(JsonObjectConst in, MeshHub::Control& c, const MeshHub::Light* toggleFrom) {
  if (in.size() == 0) {
    sendError(400, "Nothing to change. Send on, brightness, rgb, color_temp, color_temp_kelvin or color_mode.");
    return false;
  }
  for (JsonPairConst kv : in) {
    const char* k = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (!strcmp(k, "on")) {
      if (v.is<bool>()) c.on = v.as<bool>() ? 1 : 0;
      else if (v.is<int>()) c.on = v.as<int>() ? 1 : 0;
      else if (v.is<const char*>() && !strcmp(v.as<const char*>(), "toggle") && toggleFrom)
        c.on = toggleFrom->on ? 0 : 1;
      else return sendError(400, toggleFrom ? "\"on\" must be true, false or \"toggle\"." : "\"on\" must be true or false."), false;
    } else if (!strcmp(k, "brightness")) {
      if (!v.is<int>() || v.as<int>() < 0 || v.as<int>() > 255)
        return sendError(400, "\"brightness\" must be 0-255 (0 turns the light off)."), false;
      c.brightness = v.as<int>();
    } else if (!strcmp(k, "rgb")) {
      bool ok = false;
      if (v.is<JsonArrayConst>() && v.size() == 3) {
        JsonArrayConst a = v.as<JsonArrayConst>();
        ok = true;
        for (JsonVariantConst x : a) ok &= x.is<int>() && x.as<int>() >= 0 && x.as<int>() <= 255;
        if (ok) {
          c.r = a[0].as<int>();
          c.g = a[1].as<int>();
          c.b = a[2].as<int>();
        }
      } else if (v.is<const char*>()) {
        ok = hexColor(v.as<const char*>(), c);
      }
      if (!ok) return sendError(400, "\"rgb\" must be [r, g, b] (0-255 each) or \"#rrggbb\"."), false;
    } else if (!strcmp(k, "color_temp")) {
      if (!v.is<int>() || v.as<int>() < MeshHub::MIN_MIREDS || v.as<int>() > MeshHub::MAX_MIREDS)
        return sendError(400, "\"color_temp\" must be " + String(MeshHub::MIN_MIREDS) + "-" +
                                  String(MeshHub::MAX_MIREDS) + " mireds."), false;
      c.mireds = v.as<int>();
    } else if (!strcmp(k, "color_temp_kelvin")) {
      int lo = 1000000 / MeshHub::MAX_MIREDS, hi = 1000000 / MeshHub::MIN_MIREDS;
      if (!v.is<int>() || v.as<int>() < lo || v.as<int>() > hi)
        return sendError(400, "\"color_temp_kelvin\" must be " + String(lo) + "-" + String(hi) + "."), false;
      c.mireds = 1000000 / v.as<int>();
    } else if (!strcmp(k, "color_mode")) {
      const char* m = v.is<const char*>() ? v.as<const char*>() : "";
      if (!strcmp(m, "white")) c.mode = MeshHub::MODE_WHITE;
      else if (!strcmp(m, "rgb")) c.mode = MeshHub::MODE_RGB;
      else if (!strcmp(m, "color_temp")) c.mode = MeshHub::MODE_CT;
      else return sendError(400, "\"color_mode\" must be \"rgb\", \"white\" or \"color_temp\"."), false;
    } else {
      sendError(400, String("Unknown field \"") + k + "\".");
      return false;
    }
  }
  return true;
}

// ----------------------------------------------------------------- routes --

void handleInfo() {
  if (!BaseCore::authorize()) return;
  JsonDocument doc;
  doc["name"] = "BRMesh Hub";
  doc["version"] = g_version;
  doc["api"] = 1;
  doc["chip"] = CONFIG_IDF_TARGET;
  doc["hostname"] = BaseCore::hostname();
  doc["uptime_s"] = millis() / 1000;
  doc["lights"] = MeshHub::lights().size();
  doc["scanning"] = MeshHub::scanning();
  JsonObject radio = doc["radio"].to<JsonObject>();
  radio["ready"] = MeshRadio::ready();
  radio["received"] = MeshRadio::rxCount();
  radio["sent"] = MeshRadio::txCount();
  radio["dropped"] = MeshRadio::rxDropped();
  radio["queued"] = MeshHub::queueLength();
  JsonObject mq = doc["mqtt"].to<JsonObject>();
  mq["enabled"] = MeshHub::settings().mqttEnabled;
  mq["connected"] = MeshMqtt::connected();
  mq["node"] = MeshMqtt::nodeId();
  sendDoc(200, doc);
}

void handleLights() {
  if (!BaseCore::authorize()) return;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (const auto& l : MeshHub::lights()) lightJson(a.add<JsonObject>(), l);
  sendDoc(200, doc);
}

void handleAddLight() {
  if (!BaseCore::authorize()) return;
  JsonDocument in;
  if (!readBody(in)) return;
  const char* did = in["device_id"] | "";
  if (!*did) return sendError(400, "\"device_id\" is required. GET /api/v1/discovered lists lights found by a scan.");
  int addr = in["address"] | 0;
  if (!addr) {
    // Same suggestion as the web page: keep the light's own address if free.
    for (const auto& d : MeshHub::discovered())
      if (MeshHub::hexDid(d.did).equalsIgnoreCase(did) && d.reportedAddr && !MeshHub::addressUsed(d.reportedAddr))
        addr = d.reportedAddr;
    if (!addr) addr = MeshHub::nextFreeAddress();
  }
  if (addr < 1 || addr > 255) return sendError(400, "\"address\" must be 1-255.");
  String id = did;
  id.toUpperCase();
  MeshHub::Light* l = nullptr;
  String err = MeshHub::addDiscovered(id, in["name"] | "", (uint8_t)addr, &l);
  if (err.length()) return sendError(err.indexOf("already") >= 0 ? 409 : 400, err);
  sendLight(201, *l);
}

void handleGetLight() {
  if (!BaseCore::authorize()) return;
  if (MeshHub::Light* l = requireLight()) sendLight(200, *l);
}

void handleEditLight() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  JsonDocument in;
  if (!readBody(in)) return;
  for (JsonPairConst kv : in.as<JsonObjectConst>())
    if (strcmp(kv.key().c_str(), "name") && strcmp(kv.key().c_str(), "address"))
      return sendError(400, String("Unknown field \"") + kv.key().c_str() + "\". Send name and/or address.");
  if (in["name"].is<const char*>() && !strlen(in["name"].as<const char*>()))
    return sendError(400, "\"name\" can't be empty.");
  if (!in["address"].isNull() && !in["address"].is<int>()) return sendError(400, "\"address\" must be 1-255.");
  String err = MeshHub::edit(*l, in["name"] | "", in["address"] | -1, -1);
  if (err.length()) return sendError(err.indexOf("already") >= 0 ? 409 : 400, err);
  sendLight(200, *l);
}

void handleDeleteLight() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  String uid = l->uid;
  MeshHub::remove(uid);
  JsonDocument doc;
  doc["deleted"] = uid;
  sendDoc(200, doc);
}

void handleGetState() {
  if (!BaseCore::authorize()) return;
  if (WebServer::urlDecode(srv().pathArg(0)) == "all") return sendError(405, "Use PUT to set the state of all lights.");
  MeshHub::Light* l = requireLight();
  if (!l) return;
  JsonDocument doc;
  stateJson(doc.to<JsonObject>(), *l);
  sendDoc(200, doc);
}

void handleSetState() {
  if (!BaseCore::authorize()) return;
  bool all = WebServer::urlDecode(srv().pathArg(0)) == "all";
  MeshHub::Light* l = all ? nullptr : requireLight();
  if (!all && !l) return;
  JsonDocument in;
  if (!readBody(in)) return;
  MeshHub::Control c;
  if (!parseState(in.as<JsonObjectConst>(), c, l)) return;
  if (l) {
    MeshHub::control(*l, c);
    JsonDocument doc;
    stateJson(doc.to<JsonObject>(), *l);
    return sendDoc(200, doc);
  }
  for (auto& x : MeshHub::lights()) MeshHub::control(x, c);
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (const auto& x : MeshHub::lights()) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = x.uid;
    stateJson(o["state"].to<JsonObject>(), x);
  }
  sendDoc(200, doc);
}

void handleBind() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  String err = MeshHub::bind(*l);
  if (err.length()) return sendError(400, err);
  sendLight(202, *l);
}

void handleScan() {
  if (!BaseCore::authorize()) return;
  MeshHub::scan();
  JsonDocument doc;
  doc["scanning"] = true;
  sendDoc(202, doc);
}

void handleDiscovered() {
  if (!BaseCore::authorize()) return;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (const auto& d : MeshHub::discovered()) {
    JsonObject o = a.add<JsonObject>();
    String did = MeshHub::hexDid(d.did);
    o["device_id"] = did;
    o["type"] = d.type;
    o["type_name"] = fastcon::typeName(d.type);
    o["is_light"] = fastcon::isLight(d.type);
    if (d.reportedAddr) o["address"] = d.reportedAddr;
    else o["address"] = nullptr;
    o["rssi"] = d.rssi;
    o["last_seen_s"] = secondsAgo(d.lastSeen);
    bool added = false;
    for (const auto& l : MeshHub::lights()) added |= MeshHub::hexDid(l.did) == did;
    o["added"] = added;
  }
  sendDoc(200, doc);
}

void settingsJson(JsonDocument& doc) {
  const MeshHub::Settings& s = MeshHub::settings();
  char key[9];
  snprintf(key, sizeof(key), "%02X%02X%02X%02X", s.meshKey[0], s.meshKey[1], s.meshKey[2], s.meshKey[3]);
  doc["mesh_key"] = key;
  doc["tx_ms"] = s.txMs;
  doc["auto_add"] = s.autoAdd;
}

void handleGetSettings() {
  if (!BaseCore::authorize()) return;
  JsonDocument doc;
  settingsJson(doc);
  sendDoc(200, doc);
}

void handleSetSettings() {
  if (!BaseCore::authorize()) return;
  JsonDocument in;
  if (!readBody(in)) return;
  for (JsonPairConst kv : in.as<JsonObjectConst>()) {
    const char* k = kv.key().c_str();
    if (strcmp(k, "mesh_key") && strcmp(k, "tx_ms") && strcmp(k, "auto_add"))
      return sendError(400, String("Unknown field \"") + k + "\".");
  }
  if (!in["tx_ms"].isNull() && (!in["tx_ms"].is<int>() || in["tx_ms"].as<int>() < 50 || in["tx_ms"].as<int>() > 3000))
    return sendError(400, "\"tx_ms\" must be 50-3000.");
  if (!in["auto_add"].isNull() && !in["auto_add"].is<bool>()) return sendError(400, "\"auto_add\" must be true or false.");
  if (!in["mesh_key"].isNull()) {
    String err = in["mesh_key"].is<const char*>() ? MeshHub::setMeshKey(in["mesh_key"].as<const char*>())
                                                  : String("\"mesh_key\" must be a string.");
    if (err.length()) return sendError(400, err);
  }
  if (!in["tx_ms"].isNull()) MeshHub::setTxMs((uint16_t)in["tx_ms"].as<int>());
  if (!in["auto_add"].isNull()) MeshHub::setAutoAdd(in["auto_add"].as<bool>());
  JsonDocument doc;
  settingsJson(doc);
  sendDoc(200, doc);
}

void handleLog() {
  if (!BaseCore::authorize()) return;
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (const auto& line : MeshHub::recentLog()) a.add(line);
  sendDoc(200, doc);
}

}  // namespace

void begin(const char* fwVersion) {
  g_version = fwVersion;
  WebServer& s = srv();
  s.on("/api/v1", HTTP_GET, handleInfo);
  s.on("/api/v1/lights", HTTP_GET, handleLights);
  s.on("/api/v1/lights", HTTP_POST, handleAddLight);
  s.on(UriBraces("/api/v1/lights/{}"), HTTP_GET, handleGetLight);
  s.on(UriBraces("/api/v1/lights/{}"), HTTP_PUT, handleEditLight);
  s.on(UriBraces("/api/v1/lights/{}"), HTTP_PATCH, handleEditLight);
  s.on(UriBraces("/api/v1/lights/{}"), HTTP_DELETE, handleDeleteLight);
  s.on(UriBraces("/api/v1/lights/{}/state"), HTTP_GET, handleGetState);
  s.on(UriBraces("/api/v1/lights/{}/state"), HTTP_PUT, handleSetState);
  s.on(UriBraces("/api/v1/lights/{}/state"), HTTP_POST, handleSetState);
  s.on(UriBraces("/api/v1/lights/{}/bind"), HTTP_POST, handleBind);
  s.on("/api/v1/scan", HTTP_POST, handleScan);
  s.on("/api/v1/discovered", HTTP_GET, handleDiscovered);
  s.on("/api/v1/settings", HTTP_GET, handleGetSettings);
  s.on("/api/v1/settings", HTTP_PUT, handleSetSettings);
  s.on("/api/v1/settings", HTTP_PATCH, handleSetSettings);
  s.on("/api/v1/log", HTTP_GET, handleLog);
}

}  // namespace MeshApi
