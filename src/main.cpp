// BRMesh Hub: standalone ESP32-C6 firmware for BRMesh / Broadlink Fastcon
// BLE lights.
//
// Discovers lights in pairing mode, binds them into the mesh with our mesh
// key, and controls them from the web page or from Home Assistant through
// MQTT discovery. WiFi setup, device settings and firmware upload come from
// lib/BaseCore, the same as the base firmware.

#include <Arduino.h>
#include <BaseCore.h>
#include <WiFi.h>

#include "MeshApi.h"
#include "MeshHub.h"
#include "MeshMqtt.h"
#include "MeshPage.h"
#include "MeshRadio.h"
#include "MeshWeb.h"

static const char* FW_VERSION = "1.2.0";

// Mesh key used until another one is saved on the web page.
static const char* DEFAULT_MESH_KEY = "32323035";

void setup() {
  Serial.begin(115200);
  delay(200);

  MeshHub::Config hub;
  hub.defaultMeshKey = DEFAULT_MESH_KEY;
  MeshHub::begin(hub);
  MeshMqtt::begin(FW_VERSION);

  BaseConfig cfg;
  cfg.title = "BRMesh Hub";
  cfg.fwVersion = FW_VERSION;
  cfg.apSsidPrefix = "BRMesh-Hub-";
  cfg.hostnamePrefix = "brmesh-hub-";
  cfg.appHtml = MESH_HTML;
  cfg.appScript = MESH_JS;
  BaseCore::begin(cfg);
  MeshWeb::begin();
  MeshApi::begin(FW_VERSION);

  if (!MeshRadio::begin()) MeshHub::log("Bluetooth failed to start");
}

// Press Enter in the serial monitor for a status summary. Handy on boards
// using the chip's own USB port, where the boot log is gone before the port
// reconnects.
static void serialStatus() {
  if (!Serial.available()) return;
  while (Serial.available()) Serial.read();
  Serial.printf("\nBRMesh Hub v%s, %s, up %lus\n", FW_VERSION, CONFIG_IDF_TARGET,
                (unsigned long)(millis() / 1000));
  if (BaseCore::isPortalMode())
    Serial.printf("WiFi: setup AP %s at 192.168.4.1\n", WiFi.softAPSSID().c_str());
  else
    Serial.printf("WiFi: %s, %s, http://%s.local/\n", BaseCore::isConnected() ? "connected" : "reconnecting",
                  WiFi.localIP().toString().c_str(), BaseCore::hostname().c_str());
  Serial.printf("Bluetooth: %s, %lu frames received, %lu sent\n", MeshRadio::ready() ? "ready" : "not ready",
                (unsigned long)MeshRadio::rxCount(), (unsigned long)MeshRadio::txCount());
  Serial.printf("Lights: %u, MQTT: %s, free heap %lu\n", (unsigned)MeshHub::lights().size(),
                MeshMqtt::connected() ? "connected" : "not connected", (unsigned long)ESP.getFreeHeap());
  for (const auto& line : MeshHub::recentLog()) Serial.printf("  %s\n", line.c_str());
}

void loop() {
  serialStatus();
  BaseCore::loop();
  MeshRadio::loop();
  MeshHub::loop();
  MeshMqtt::loop();
}
