#include "MeshWeb.h"

#include <ArduinoJson.h>
#include <BaseCore.h>

#include "MeshHub.h"
#include "MeshMqtt.h"
#include "MeshRadio.h"

namespace MeshWeb {

namespace {

const uint16_t LIGHT_TYPES[] = {
    fastcon::TYPE_GENERIC,     fastcon::TYPE_LIGHT_RGBCW,     fastcon::TYPE_LIGHT_RGBW,
    fastcon::TYPE_LIGHT_RGB,   fastcon::TYPE_LIGHT_CCT,       fastcon::TYPE_LIGHT_W_CW,
    fastcon::TYPE_LIGHT_PWR,   fastcon::TYPE_LIGHT_BURDEN_W,  fastcon::TYPE_LIGHT_BURDEN_CW,
    fastcon::TYPE_LIGHT_COMPOSE,
};

WebServer& srv() { return BaseCore::server(); }

void sendJson(int code, const String& body) {
  srv().sendHeader("Cache-Control", "no-store");
  srv().send(code, "application/json", body);
}

void sendResult(const String& error, const String& okMessage = "Done.") {
  JsonDocument doc;
  doc["ok"] = error.isEmpty();
  doc["message"] = error.isEmpty() ? okMessage : error;
  String out;
  serializeJson(doc, out);
  sendJson(error.isEmpty() ? 200 : 400, out);
}

int argInt(const char* name, int fallback = -1) {
  if (!srv().hasArg(name) || srv().arg(name).isEmpty()) return fallback;
  return srv().arg(name).toInt();
}

int32_t secondsAgo(uint32_t t) { return t ? (int32_t)((millis() - t) / 1000) : -1; }

MeshHub::Light* requireLight() {
  MeshHub::Light* l = MeshHub::find(srv().arg("uid"));
  if (!l) sendResult("Unknown light.");
  return l;
}

void handleState() {
  if (!BaseCore::authorize()) return;
  const MeshHub::Settings& s = MeshHub::settings();
  JsonDocument doc;
  char key[9];
  snprintf(key, sizeof(key), "%02X%02X%02X%02X", s.meshKey[0], s.meshKey[1], s.meshKey[2],
           s.meshKey[3]);
  doc["meshKey"] = key;
  doc["txMs"] = s.txMs;
  doc["autoAdd"] = s.autoAdd;
  doc["scanning"] = MeshHub::scanning();
  doc["nextAddr"] = MeshHub::nextFreeAddress();

  JsonObject radio = doc["radio"].to<JsonObject>();
  radio["ready"] = MeshRadio::ready();
  radio["rx"] = MeshRadio::rxCount();
  radio["tx"] = MeshRadio::txCount();
  radio["dropped"] = MeshRadio::rxDropped();
  radio["queue"] = MeshHub::queueLength();

  JsonObject mq = doc["mqtt"].to<JsonObject>();
  mq["enabled"] = s.mqttEnabled;
  mq["host"] = s.mqttHost;
  mq["port"] = s.mqttPort;
  mq["user"] = s.mqttUser;
  mq["passSet"] = s.mqttPass.length() > 0;
  mq["prefix"] = s.mqttPrefix;
  mq["connected"] = MeshMqtt::connected();
  mq["node"] = MeshMqtt::nodeId();

  JsonArray lights = doc["lights"].to<JsonArray>();
  for (const auto& l : MeshHub::lights()) {
    JsonObject o = lights.add<JsonObject>();
    o["uid"] = l.uid;
    o["name"] = l.name;
    o["addr"] = l.addr;
    o["type"] = l.type;
    o["typeName"] = fastcon::typeName(l.type);
    o["caps"] = MeshHub::supportedCaps(l);
    o["hasDid"] = l.hasDid;
    if (l.hasDid) o["did"] = MeshHub::hexDid(l.did);
    o["on"] = l.on;
    o["bri"] = l.brightness;
    o["mode"] = l.mode;
    char rgb[7];
    snprintf(rgb, sizeof(rgb), "%02x%02x%02x", l.r, l.g, l.b);
    o["rgb"] = rgb;
    o["mireds"] = l.mireds;
    o["heard"] = secondsAgo(l.lastHeard);
    o["pending"] = l.bindPending;
    o["topic"] = MeshMqtt::lightTopic(l);
  }

  JsonArray disc = doc["discovered"].to<JsonArray>();
  for (const auto& d : MeshHub::discovered()) {
    JsonObject o = disc.add<JsonObject>();
    String did = MeshHub::hexDid(d.did);
    o["did"] = did;
    o["short"] = did.substring(8);
    o["type"] = d.type;
    o["typeName"] = fastcon::typeName(d.type);
    o["isLight"] = fastcon::isLight(d.type);
    o["addr"] = d.reportedAddr;
    o["rssi"] = d.rssi;
    o["ago"] = secondsAgo(d.lastSeen);
    o["suggest"] = (d.reportedAddr && !MeshHub::addressUsed(d.reportedAddr))
                       ? d.reportedAddr
                       : MeshHub::nextFreeAddress();
  }

  JsonArray types = doc["types"].to<JsonArray>();
  for (uint16_t t : LIGHT_TYPES) {
    JsonArray pair = types.add<JsonArray>();
    pair.add(t);
    pair.add(fastcon::typeName(t));
  }

  JsonArray log = doc["log"].to<JsonArray>();
  for (const auto& line : MeshHub::recentLog()) log.add(line);

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleScan() {
  if (!BaseCore::authorize()) return;
  MeshHub::scan();
  sendResult("", "Scanning. Lights in pairing mode appear below within a few seconds.");
}

void handleAdd() {
  if (!BaseCore::authorize()) return;
  int addr = argInt("addr", 0);
  if (addr < 1 || addr > 255) return sendResult("Mesh address must be 1-255.");
  String name = srv().arg("name");
  if (!srv().arg("did").length()) return sendResult("Scan for the light and add it from the list.");
  String err = MeshHub::addDiscovered(srv().arg("did"), name, (uint8_t)addr);
  sendResult(err, "Added. Binding it to address " + String(addr) + "…");
}

void handleEdit() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  sendResult(MeshHub::edit(*l, srv().arg("name"), argInt("addr"), argInt("type")), "Saved.");
}

void handleBind() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  sendResult(MeshHub::bind(*l),
             "Bind sent. If the light doesn't confirm, switch it off and on again; it is bound "
             "as soon as it announces itself.");
}

void handleRemove() {
  if (!BaseCore::authorize()) return;
  MeshHub::remove(srv().arg("uid"));
  sendResult("", "Removed.");
}

void handleControl() {
  if (!BaseCore::authorize()) return;
  MeshHub::Light* l = requireLight();
  if (!l) return;
  MeshHub::Control c;
  if (srv().hasArg("on")) c.on = srv().arg("on") == "1" ? 1 : 0;
  c.brightness = argInt("bri");
  String rgb = srv().arg("rgb");
  if (rgb.length() == 6) {
    long v = strtol(rgb.c_str(), nullptr, 16);
    c.r = (v >> 16) & 0xFF;
    c.g = (v >> 8) & 0xFF;
    c.b = v & 0xFF;
  }
  c.mireds = argInt("mireds");
  String mode = srv().arg("mode");
  if (mode == "white") c.mode = MeshHub::MODE_WHITE;
  else if (mode == "ct") c.mode = MeshHub::MODE_CT;
  else if (mode == "rgb") c.mode = MeshHub::MODE_RGB;
  MeshHub::control(*l, c);
  sendResult("");
}

void handleAll() {
  if (!BaseCore::authorize()) return;
  MeshHub::allOnOff(srv().arg("on") == "1");
  sendResult("");
}

void handleSettings() {
  if (!BaseCore::authorize()) return;
  if (srv().hasArg("key")) {
    String err = MeshHub::setMeshKey(srv().arg("key"));
    if (err.length()) return sendResult(err);
  }
  if (srv().hasArg("txms")) MeshHub::setTxMs((uint16_t)argInt("txms", 300));
  if (srv().hasArg("autoadd")) MeshHub::setAutoAdd(srv().arg("autoadd") == "1");
  sendResult("", "Saved.");
}

void handleMqtt() {
  if (!BaseCore::authorize()) return;
  const MeshHub::Settings& s = MeshHub::settings();
  bool enabled = srv().arg("enabled") == "1";
  String host = srv().arg("host");
  host.trim();
  if (enabled && host.isEmpty()) return sendResult("Enter the MQTT broker address.");
  int port = argInt("port", 1883);
  if (port < 1 || port > 65535) return sendResult("Port must be 1-65535.");
  String pass = srv().hasArg("pass") && srv().arg("pass").length() ? srv().arg("pass") : s.mqttPass;
  if (srv().arg("passClear") == "1") pass = "";
  String prefix = srv().arg("prefix");
  prefix.trim();
  MeshHub::setMqtt(enabled, host, (uint16_t)port, srv().arg("user"), pass, prefix);
  sendResult("", enabled ? "Saved. Connecting to the broker…" : "Saved. MQTT is off.");
}

}  // namespace

void begin() {
  WebServer& s = srv();
  s.on("/api/mesh", HTTP_GET, handleState);
  s.on("/api/mesh/scan", HTTP_POST, handleScan);
  s.on("/api/mesh/add", HTTP_POST, handleAdd);
  s.on("/api/mesh/edit", HTTP_POST, handleEdit);
  s.on("/api/mesh/bind", HTTP_POST, handleBind);
  s.on("/api/mesh/remove", HTTP_POST, handleRemove);
  s.on("/api/mesh/control", HTTP_POST, handleControl);
  s.on("/api/mesh/all", HTTP_POST, handleAll);
  s.on("/api/mesh/settings", HTTP_POST, handleSettings);
  s.on("/api/mesh/mqtt", HTTP_POST, handleMqtt);
}

}  // namespace MeshWeb
