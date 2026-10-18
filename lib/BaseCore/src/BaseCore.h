#pragma once

#include <Arduino.h>
#include <WebServer.h>

struct BaseConfig {
  const char* title = "ESPC3-12 Base Firmware";  // web page title / heading
  const char* fwVersion = "1.0.0";
  const char* apSsidPrefix = "ESPC3-12-";        // + last 4 hex of MAC
  const char* apPassword = "";                   // "" = open AP, else 8-63 chars
  const char* hostnamePrefix = "espc3-";         // default hostname + MAC suffix
  uint32_t staConnectTimeoutMs = 20000;          // per connection attempt
  uint32_t staRetryIntervalMs = 300000;          // portal-mode retry / lost-link fallback
  int resetButtonPin = 9;                        // BOOT button, active low; -1 disables
  uint32_t resetHoldMs = 5000;                   // hold to erase WiFi credentials
  // Optional application UI on the main page: HTML sections shown above the
  // base cards, and a script run after the base script (so it can use its
  // helpers: $, esc, post, setMsg, handleResult). App cards stay on the main
  // tab unless tagged <section class="card" data-page="settings">; the base
  // cards are on the Settings tab.
  const char* appHtml = nullptr;
  const char* appScript = nullptr;
};

namespace BaseCore {

void begin(const BaseConfig& cfg = BaseConfig());
void loop();

// The shared web server (port 80). Application code may register extra routes.
WebServer& server();

// Call at the top of a route handler; returns false (and has already sent a
// 401 challenge) when an admin password is set and the request lacks it.
bool authorize();

bool isPortalMode();
bool isConnected();
String hostname();
void scheduleRestart(uint32_t delayMs = 1000);

}  // namespace BaseCore
