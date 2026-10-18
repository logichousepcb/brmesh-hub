#include "MeshMqtt.h"

#include <ArduinoJson.h>
#include <BaseCore.h>
#include <WiFi.h>
#include <mqtt_client.h>

#include <deque>
#include <map>
#include <mutex>

#include "MeshHub.h"

namespace MeshMqtt {

namespace {

struct Inbound {
  String topic;
  String payload;
};

const char* g_fw = "";
String g_node;   // brmesh_<mac>
String g_base;   // brmesh/<node>
esp_mqtt_client_handle_t g_client = nullptr;
volatile bool g_connected = false;
volatile bool g_justConnected = false;
bool g_restart = false;

std::mutex g_inboxLock;
std::deque<Inbound> g_inbox;

std::map<String, String> g_published;  // uid -> topic name its state was last published under

String topicBase(const String& name) { return g_base + "/light/" + name; }
String configTopic(const char* component, const String& object) {
  return MeshHub::settings().mqttPrefix + "/" + component + "/" + g_node + "/" + object + "/config";
}

void publish(const String& topic, const String& payload, bool retain) {
  if (!g_client || !g_connected) return;
  esp_mqtt_client_publish(g_client, topic.c_str(), payload.c_str(), payload.length(), 0,
                          retain ? 1 : 0);
}

const char* modeName(uint8_t mode) {
  switch (mode) {
    case MeshHub::MODE_RGB: return "rgb";
    case MeshHub::MODE_CT: return "color_temp";
    case MeshHub::MODE_WHITE: return "white";
    case MeshHub::MODE_BRIGHTNESS: return "brightness";
    default: return "onoff";
  }
}

void publishState(const MeshHub::Light& l) {
  uint8_t caps = MeshHub::supportedCaps(l);
  JsonDocument doc;
  doc["state"] = l.on ? "ON" : "OFF";
  doc["color_mode"] = modeName(l.mode);
  if (caps & fastcon::CAP_DIM) doc["brightness"] = l.brightness;
  if (caps & fastcon::CAP_RGB) {
    JsonObject c = doc["color"].to<JsonObject>();
    c["r"] = l.r;
    c["g"] = l.g;
    c["b"] = l.b;
  }
  if (caps & fastcon::CAP_CT) doc["color_temp"] = l.mireds;
  String out;
  serializeJson(doc, out);
  publish(lightTopic(l) + "/state", out, true);
}

void addDevice(JsonDocument& doc, const MeshHub::Light* l) {
  JsonObject dev = doc["device"].to<JsonObject>();
  if (l) {
    dev["identifiers"].add(g_node + "_" + l->uid);
    dev["name"] = l->name;
    dev["manufacturer"] = "BRMesh";
    dev["model"] = String("BRMesh ") + fastcon::typeName(l->type);
    dev["via_device"] = g_node;
  } else {
    dev["identifiers"].add(g_node);
    dev["name"] = BaseCore::hostname();
    dev["manufacturer"] = "BRMesh";
    dev["model"] = "BRMesh Hub (" CONFIG_IDF_TARGET ")";
    dev["sw_version"] = g_fw;
    dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
  }
}

void publishLightConfig(const MeshHub::Light& l) {
  uint8_t caps = MeshHub::supportedCaps(l);
  JsonDocument doc;
  doc["name"] = nullptr;  // entity takes the device (light) name
  doc["unique_id"] = g_node + "_" + l.uid;
  doc["schema"] = "json";
  doc["command_topic"] = lightTopic(l) + "/set";
  doc["state_topic"] = lightTopic(l) + "/state";
  doc["availability_topic"] = g_base + "/status";
  doc["brightness"] = (caps & fastcon::CAP_DIM) != 0;
  JsonArray modes = doc["supported_color_modes"].to<JsonArray>();
  if (caps & fastcon::CAP_RGB) modes.add("rgb");
  if (caps & fastcon::CAP_CT) modes.add("color_temp");
  if (caps & fastcon::CAP_WHITE) modes.add("white");
  if (modes.size() == 0) modes.add((caps & fastcon::CAP_DIM) ? "brightness" : "onoff");
  if (caps & fastcon::CAP_CT) {
    doc["min_mireds"] = MeshHub::MIN_MIREDS;
    doc["max_mireds"] = MeshHub::MAX_MIREDS;
  }
  addDevice(doc, &l);
  String out;
  serializeJson(doc, out);
  publish(configTopic("light", l.uid), out, true);
}

void publishHubConfig() {
  JsonDocument doc;
  doc["name"] = "Scan for lights";
  doc["unique_id"] = g_node + "_scan";
  doc["command_topic"] = g_base + "/scan";
  doc["availability_topic"] = g_base + "/status";
  doc["icon"] = "mdi:magnify-scan";
  addDevice(doc, nullptr);
  String out;
  serializeJson(doc, out);
  publish(configTopic("button", "scan"), out, true);
}

// Clears the retained state left under a light's previous topic name.
void forgetOldTopic(const MeshHub::Light& l) {
  if (!g_connected) return;  // publishAll() on connect catches up
  String now = MeshHub::topicName(l);
  auto it = g_published.find(l.uid);
  if (it != g_published.end() && it->second != now) publish(topicBase(it->second) + "/state", "", true);
  g_published[l.uid] = now;
}

void publishLight(const MeshHub::Light& l) {
  forgetOldTopic(l);
  publishLightConfig(l);
  publishState(l);
}

void publishAll() {
  publish(g_base + "/status", "online", true);
  publishHubConfig();
  for (const auto& l : MeshHub::lights()) {
    // Before 1.2 the topics used the device ID; clear what is left there.
    if (MeshHub::topicName(l) != l.uid) publish(topicBase(l.uid) + "/state", "", true);
    publishLight(l);
  }
}

void onEvent(void*, esp_event_base_t, int32_t id, void* data) {
  auto* e = static_cast<esp_mqtt_event_handle_t>(data);
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      g_connected = true;
      g_justConnected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      g_connected = false;
      break;
    case MQTT_EVENT_DATA:
      // Commands are small; ignore anything split over several events.
      if (e->current_data_offset == 0 && e->data_len == e->total_data_len && e->data_len < 1024) {
        std::lock_guard<std::mutex> lock(g_inboxLock);
        if (g_inbox.size() < 16)
          g_inbox.push_back({String(e->topic, e->topic_len), String(e->data, e->data_len)});
      }
      break;
    default:
      break;
  }
}

void stop() {
  if (!g_client) return;
  esp_mqtt_client_stop(g_client);
  esp_mqtt_client_destroy(g_client);
  g_client = nullptr;
  g_connected = false;
  g_justConnected = false;
}

void start() {
  const MeshHub::Settings& s = MeshHub::settings();
  String uri = "mqtt://" + s.mqttHost + ":" + String(s.mqttPort);
  String lwt = g_base + "/status";
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = uri.c_str();
  cfg.credentials.client_id = g_node.c_str();
  if (s.mqttUser.length()) cfg.credentials.username = s.mqttUser.c_str();
  if (s.mqttPass.length()) cfg.credentials.authentication.password = s.mqttPass.c_str();
  cfg.session.last_will.topic = lwt.c_str();
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.session.keepalive = 30;
  cfg.network.reconnect_timeout_ms = 10000;
  cfg.buffer.size = 2048;
  g_client = esp_mqtt_client_init(&cfg);  // copies the strings
  if (!g_client) {
    MeshHub::log("MQTT: client init failed");
    return;
  }
  esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, onEvent, nullptr);
  esp_mqtt_client_start(g_client);
  MeshHub::log("MQTT: connecting to %s", uri.c_str());
}

void handleCommand(MeshHub::Light& l, const String& payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;
  MeshHub::Control c;
  if (doc["state"].is<const char*>()) c.on = strcmp(doc["state"], "ON") == 0 ? 1 : 0;
  if (doc["brightness"].is<int>()) c.brightness = doc["brightness"].as<int>();
  if (doc["color"].is<JsonObject>()) {
    c.r = doc["color"]["r"] | 0;
    c.g = doc["color"]["g"] | 0;
    c.b = doc["color"]["b"] | 0;
  }
  if (doc["color_temp"].is<int>()) c.mireds = doc["color_temp"].as<int>();
  if (doc["white"].is<int>()) {
    c.mode = MeshHub::MODE_WHITE;
    c.brightness = doc["white"].as<int>();
  }
  MeshHub::control(l, c);
}

void handleInbound(const Inbound& m) {
  const MeshHub::Settings& s = MeshHub::settings();
  if (m.topic == s.mqttPrefix + "/status") {
    if (m.payload == "online") publishAll();  // Home Assistant restarted
    return;
  }
  if (m.topic == g_base + "/scan") {
    MeshHub::scan();
    return;
  }
  String prefix = g_base + "/light/";
  if (m.topic.startsWith(prefix) && m.topic.endsWith("/set")) {
    String name = m.topic.substring(prefix.length(), m.topic.length() - 4);
    MeshHub::Light* l = MeshHub::findByTopic(name);
    if (!l) l = MeshHub::find(name);  // device ID still works
    if (l) handleCommand(*l, m.payload);
  }
}

}  // namespace

void begin(const char* fwVersion) {
  g_fw = fwVersion;
  uint64_t mac = ESP.getEfuseMac();
  char id[20];
  snprintf(id, sizeof(id), "brmesh_%02x%02x%02x", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 40));
  g_node = id;
  g_base = "brmesh/" + g_node;

  MeshHub::Listener l;
  l.stateChanged = [](const MeshHub::Light& x) { publishState(x); };
  l.lightAdded = [](const MeshHub::Light& x) { publishLight(x); };
  l.lightRemoved = [](const String& uid) {
    publish(configTopic("light", uid), "", true);
    auto it = g_published.find(uid);
    if (it != g_published.end()) {
      publish(topicBase(it->second) + "/state", "", true);
      g_published.erase(it);
    }
  };
  l.settingsChanged = [] { g_restart = true; };
  MeshHub::setListener(l);
}

void loop() {
  const MeshHub::Settings& s = MeshHub::settings();
  bool wanted = s.mqttEnabled && s.mqttHost.length() && BaseCore::isConnected() &&
                !BaseCore::isPortalMode();
  if (g_restart || (!wanted && g_client)) {
    g_restart = false;
    stop();
  }
  if (wanted && !g_client) start();

  if (g_justConnected) {
    g_justConnected = false;
    MeshHub::log("MQTT: connected");
    esp_mqtt_client_subscribe_single(g_client, (g_base + "/light/+/set").c_str(), 0);
    esp_mqtt_client_subscribe_single(g_client, (g_base + "/scan").c_str(), 0);
    esp_mqtt_client_subscribe_single(g_client, (s.mqttPrefix + "/status").c_str(), 0);
    publishAll();
  }

  for (int i = 0; i < 8; i++) {
    Inbound m;
    {
      std::lock_guard<std::mutex> lock(g_inboxLock);
      if (g_inbox.empty()) break;
      m = g_inbox.front();
      g_inbox.pop_front();
    }
    handleInbound(m);
  }
}

bool connected() { return g_connected; }
String lightTopic(const MeshHub::Light& l) { return topicBase(MeshHub::topicName(l)); }
String nodeId() { return g_node; }

}  // namespace MeshMqtt
