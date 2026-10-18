// BRMesh hub: the list of lights (kept in NVS), discovery of new lights,
// binding them into the mesh, and the transmit queue. Everything here runs
// in the Arduino loop task.

#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include "Fastcon.h"

namespace MeshHub {

enum ColorMode : uint8_t { MODE_ONOFF, MODE_BRIGHTNESS, MODE_RGB, MODE_WHITE, MODE_CT };

constexpr uint16_t MIN_MIREDS = 153;
constexpr uint16_t MAX_MIREDS = 500;
constexpr size_t MAX_LIGHTS = 64;

struct Light {
  // Persistent
  char uid[17];       // DID hex for discovered lights, "m" + random for manual ones
  char name[33];
  uint16_t type;      // fastcon::DeviceType
  uint8_t did[6];
  uint8_t key[4];     // device key (needed to bind)
  bool hasDid;        // false: added by address only, can't be (re)bound
  uint8_t addr;       // mesh address 1-255
  // Assumed state (lights don't report it)
  bool on;
  uint8_t brightness; // 1-255
  uint8_t mode;       // ColorMode
  uint8_t r, g, b;
  uint16_t mireds;
  // Runtime
  uint32_t lastHeard;   // millis() of the last heartbeat/announcement, 0 = never
  bool bindPending;     // bind sent, waiting for a heartbeat from the new address
  uint32_t lastBind;
};

struct Discovered {
  uint8_t did[6];
  uint16_t type;
  uint8_t key[4];
  uint8_t reportedAddr;
  int8_t rssi;
  uint32_t lastSeen;
  uint32_t count;
};

struct Heard {  // mesh address seen in a heartbeat but not in our list
  uint16_t addr;
  uint32_t lastSeen;
  uint32_t count;
};

struct Settings {
  uint8_t meshKey[4];
  uint16_t txMs;       // how long each light command is broadcast
  bool autoAdd;        // add + bind every light a scan finds
  bool mqttEnabled;
  String mqttHost;
  uint16_t mqttPort;
  String mqttUser;
  String mqttPass;
  String mqttPrefix;   // Home Assistant discovery prefix
};

// A change requested from the web page or MQTT. Unset fields keep their value.
struct Control {
  int8_t on = -1;          // 0/1, -1 = unchanged
  int16_t brightness = -1; // 0-255
  int16_t r = -1, g = -1, b = -1;
  int16_t mireds = -1;
  int8_t mode = -1;        // ColorMode
};

struct Config {
  const char* defaultMeshKey = "A1A2A3A4";  // 8 hex chars
};

void begin(const Config& cfg);
void loop();

// Lights
std::vector<Light>& lights();
Light* find(const String& uid);
// MQTT topic name from a light name: lower case, runs of other characters
// become "_" ("Porch Light 2" -> "porch_light_2"). Unique across lights.
String topicName(const String& name);
String topicName(const Light& l);
Light* findByTopic(const String& topic);
String hexDid(const uint8_t did[6]);
uint8_t nextFreeAddress();
bool addressUsed(uint8_t addr, const Light* except = nullptr);

String addDiscovered(const String& didHex, const String& name, uint8_t addr, Light** out = nullptr);
String edit(Light& l, const String& name, int addr, int type);
String bind(Light& l);
void remove(const String& uid);
void control(Light& l, const Control& c);
void allOnOff(bool on);
uint8_t supportedCaps(const Light& l);

// Discovery
void scan();
bool scanning();
const std::vector<Discovered>& discovered();
const std::vector<Heard>& heard();

// Settings
const Settings& settings();
String setMeshKey(const String& hex);
void setTxMs(uint16_t ms);
void setAutoAdd(bool on);
void setMqtt(bool enabled, const String& host, uint16_t port, const String& user,
             const String& pass, const String& prefix);

// Activity log, newest last.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::vector<String> recentLog();
size_t queueLength();

// Notifications for the MQTT bridge.
struct Listener {
  std::function<void(const Light&)> stateChanged;
  std::function<void(const Light&)> lightAdded;       // also on rename / type change
  std::function<void(const String& uid)> lightRemoved;
  std::function<void()> settingsChanged;
};
void setListener(const Listener& l);

}  // namespace MeshHub
