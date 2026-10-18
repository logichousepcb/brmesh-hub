#include "BaseCore.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "BasePage.h"

namespace {

const char* PREFS_NS = "basecore";
const char* ADMIN_USER = "admin";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

BaseConfig g_cfg;
WebServer g_server(80);
DNSServer g_dns;
Preferences g_prefs;

String g_ssid, g_pass, g_host, g_admin;
String g_apSsid;
String g_macSuffix;

bool g_portal = false;
bool g_restartPending = false;
uint32_t g_restartAt = 0;

uint32_t g_lastRetry = 0;       // portal mode: when the last STA attempt began
bool g_retrying = false;        // portal mode: STA attempt in progress
uint32_t g_linkLostAt = 0;      // station mode: when the link dropped (0 = up)
uint32_t g_btnDownAt = 0;

bool g_updateStarted = false;
bool g_updateAuthOk = false;
bool g_updateFirstChunk = true;
String g_updateError;

// ---------------------------------------------------------------- helpers --

bool elapsed(uint32_t since, uint32_t ms) { return (uint32_t)(millis() - since) >= ms; }

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

String jsonStr(const String& s) { return "\"" + jsonEscape(s) + "\""; }

void sendJson(int code, const String& body) {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(code, "application/json", body);
}

void sendResult(bool ok, const String& msg, bool restart = false) {
  sendJson(ok ? 200 : 400, String("{\"ok\":") + (ok ? "true" : "false") +
                               ",\"message\":" + jsonStr(msg) +
                               ",\"restart\":" + (restart ? "true" : "false") + "}");
}

bool validHostname(const String& h) {
  if (h.length() < 1 || h.length() > 32) return false;
  if (h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h[i];
    if (!(isdigit(c) || (c >= 'a' && c <= 'z') || c == '-')) return false;
  }
  return true;
}

// ---------------------------------------------------------------- storage --

void loadSettings() {
  g_prefs.begin(PREFS_NS, true);
  g_ssid = g_prefs.getString("ssid", "");
  g_pass = g_prefs.getString("pass", "");
  g_host = g_prefs.getString("host", "");
  g_admin = g_prefs.getString("admin", "");
  g_prefs.end();

  uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  g_macSuffix = suffix;

  if (!validHostname(g_host)) {
    g_host = String(g_cfg.hostnamePrefix) + g_macSuffix;
    g_host.toLowerCase();
  }
  g_apSsid = String(g_cfg.apSsidPrefix) + g_macSuffix;
}

void putString(const char* key, const String& value) {
  g_prefs.begin(PREFS_NS, false);
  g_prefs.putString(key, value);
  g_prefs.end();
}

void forgetWifi() {
  g_prefs.begin(PREFS_NS, false);
  g_prefs.remove("ssid");
  g_prefs.remove("pass");
  g_prefs.end();
  g_ssid = "";
  g_pass = "";
  WiFi.disconnect(false, true);  // also clear anything the IDF stored itself
}

// ------------------------------------------------------------------- WiFi --

bool connectStation() {
  if (g_ssid.isEmpty()) return false;

  Serial.printf("[wifi] connecting to \"%s\"\n", g_ssid.c_str());
  WiFi.setHostname(g_host.c_str());  // must precede mode()/begin() on 2.x
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && !elapsed(start, g_cfg.staConnectTimeoutMs)) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] connection failed");
    WiFi.disconnect(true);
    return false;
  }

  Serial.printf("[wifi] connected, IP %s, http://%s.local/\n",
                WiFi.localIP().toString().c_str(), g_host.c_str());
  if (MDNS.begin(g_host.c_str())) MDNS.addService("http", "tcp", 80);
  return true;
}

void startPortal() {
  Serial.printf("[portal] starting AP \"%s\" at %s\n", g_apSsid.c_str(),
                AP_IP.toString().c_str());
  WiFi.setHostname(g_host.c_str());
  WiFi.mode(WIFI_AP_STA);  // STA side is needed for scanning and retries
  WiFi.setAutoReconnect(false);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  const char* pass = (g_cfg.apPassword && strlen(g_cfg.apPassword) >= 8) ? g_cfg.apPassword : nullptr;
  WiFi.softAP(g_apSsid.c_str(), pass);

  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", AP_IP);

  g_portal = true;
  g_retrying = false;
  g_lastRetry = millis();
}

// Portal mode: periodically retry the saved network, but only while nobody is
// connected to the AP (a STA attempt can move the radio channel under them).
void portalRetryTick() {
  if (g_ssid.isEmpty()) return;

  if (g_retrying) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[portal] saved network is back, restarting into station mode");
      BaseCore::scheduleRestart(500);
      g_retrying = false;
    } else if (elapsed(g_lastRetry, g_cfg.staConnectTimeoutMs)) {
      WiFi.disconnect(false);
      g_retrying = false;
      g_lastRetry = millis();
    }
    return;
  }

  if (elapsed(g_lastRetry, g_cfg.staRetryIntervalMs) && WiFi.softAPgetStationNum() == 0) {
    Serial.println("[portal] retrying saved network");
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    g_retrying = true;
    g_lastRetry = millis();
  }
}

// Station mode: if the link stays down too long, bring up the portal so the
// device can still be reached / reconfigured.
void stationWatchTick() {
  if (WiFi.status() == WL_CONNECTED) {
    g_linkLostAt = 0;
    return;
  }
  if (g_linkLostAt == 0) {
    g_linkLostAt = millis() | 1;
    Serial.println("[wifi] link lost, auto-reconnecting");
  } else if (elapsed(g_linkLostAt, g_cfg.staRetryIntervalMs)) {
    Serial.println("[wifi] link down too long, falling back to setup portal");
    g_linkLostAt = 0;
    MDNS.end();
    startPortal();
  }
}

void resetButtonTick() {
  if (g_cfg.resetButtonPin < 0) return;
  if (digitalRead(g_cfg.resetButtonPin) == LOW) {
    if (g_btnDownAt == 0) {
      g_btnDownAt = millis() | 1;
    } else if (elapsed(g_btnDownAt, g_cfg.resetHoldMs)) {
      Serial.println("[reset] button held, erasing WiFi credentials");
      forgetWifi();
      ESP.restart();
    }
  } else {
    g_btnDownAt = 0;
  }
}

// --------------------------------------------------------------- handlers --

bool isOwnHost() {
  String h = g_server.hostHeader();
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  return h.isEmpty() || h == AP_IP.toString() || h == WiFi.localIP().toString() ||
         h == g_host || h == g_host + ".local";
}

// In portal mode, requests for any other host (OS captive-portal probes,
// typed URLs) are redirected to the setup page so the phone/laptop pops it up.
bool captiveRedirect() {
  if (!g_portal || isOwnHost()) return false;
  g_server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
  g_server.send(302, "text/plain", "");
  return true;
}

void handleRoot() {
  if (captiveRedirect()) return;
  if (!BaseCore::authorize()) return;
  String page = FPSTR(BASE_PAGE_HTML);
  page.replace("%TITLE%", g_cfg.title);
  page.replace("%CHIP%", CONFIG_IDF_TARGET);
  page.replace("%APP_HTML%", g_cfg.appHtml ? g_cfg.appHtml : "");
  page.replace("%APP_JS%", g_cfg.appScript ? g_cfg.appScript : "");
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "text/html", page);
}

void handleStatus() {
  if (!BaseCore::authorize()) return;
  bool connected = WiFi.status() == WL_CONNECTED;
  const esp_partition_t* running = esp_ota_get_running_partition();

  String j = "{";
  j += "\"title\":" + jsonStr(g_cfg.title);
  j += ",\"version\":" + jsonStr(g_cfg.fwVersion);
  j += ",\"build\":" + jsonStr(String(__DATE__) + " " + __TIME__);
  j += ",\"mode\":" + jsonStr(g_portal ? "portal" : "station");
  j += ",\"savedSsid\":" + jsonStr(g_ssid);
  j += ",\"connected\":" + String(connected ? "true" : "false");
  j += ",\"ip\":" + jsonStr(connected ? WiFi.localIP().toString() : "");
  j += ",\"rssi\":" + String(connected ? WiFi.RSSI() : 0);
  j += ",\"apSsid\":" + jsonStr(g_portal ? g_apSsid : "");
  j += ",\"apIp\":" + jsonStr(g_portal ? AP_IP.toString() : "");
  j += ",\"apClients\":" + String(g_portal ? WiFi.softAPgetStationNum() : 0);
  j += ",\"hostname\":" + jsonStr(g_host);
  j += ",\"mac\":" + jsonStr(WiFi.macAddress());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"chip\":" + jsonStr(String(ESP.getChipModel()) + " rev " + ESP.getChipRevision());
  j += ",\"flash\":" + String(ESP.getFlashChipSize());
  j += ",\"sketch\":" + String(ESP.getSketchSize());
  j += ",\"otaSpace\":" + String(ESP.getFreeSketchSpace());
  j += ",\"partition\":" + jsonStr(running ? running->label : "?");
  j += ",\"sdk\":" + jsonStr(ESP.getSdkVersion());
  j += ",\"authEnabled\":" + String(g_admin.isEmpty() ? "false" : "true");
  j += "}";
  sendJson(200, j);
}

void handleScan() {
  if (!BaseCore::authorize()) return;
  int n = WiFi.scanComplete();
  if (g_server.hasArg("fresh") && n >= 0) {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    sendJson(200, "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    sendJson(200, "{\"scanning\":true}");
    return;
  }

  // De-duplicate by SSID, keeping the strongest entry.
  String j = "{\"scanning\":false,\"networks\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int k = 0; k < n && !dup; k++) {
      if (k != i && WiFi.SSID(k) == ssid &&
          (WiFi.RSSI(k) > WiFi.RSSI(i) || (WiFi.RSSI(k) == WiFi.RSSI(i) && k < i))) {
        dup = true;
      }
    }
    if (dup) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"ssid\":" + jsonStr(ssid) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  sendJson(200, j);
}

void handleWifiSave() {
  if (!BaseCore::authorize()) return;
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32) return sendResult(false, "SSID must be 1-32 characters.");
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63))
    return sendResult(false, "Password must be empty (open network) or 8-63 characters.");

  g_prefs.begin(PREFS_NS, false);
  g_prefs.putString("ssid", ssid);
  g_prefs.putString("pass", pass);
  g_prefs.end();
  g_ssid = ssid;
  g_pass = pass;

  sendResult(true, "Saved. Restarting to connect to \"" + ssid + "\". If it can't connect, the setup AP \"" +
                       g_apSsid + "\" will come back.", true);
  BaseCore::scheduleRestart(1500);
}

void handleDeviceSave() {
  if (!BaseCore::authorize()) return;
  bool restart = false;

  if (g_server.hasArg("hostname")) {
    String h = g_server.arg("hostname");
    h.trim();
    h.toLowerCase();
    if (!validHostname(h))
      return sendResult(false, "Hostname: 1-32 chars, a-z, 0-9 and '-', not starting/ending with '-'.");
    if (h != g_host) {
      putString("host", h);
      g_host = h;
      restart = true;
    }
  }

  if (g_server.arg("adminClear") == "1") {
    putString("admin", "");
    g_admin = "";
  } else if (g_server.arg("admin").length() > 0) {
    String a = g_server.arg("admin");
    if (a.length() < 4 || a.length() > 64) return sendResult(false, "Admin password must be 4-64 characters.");
    putString("admin", a);
    g_admin = a;
  }

  sendResult(true, restart ? "Saved. Restarting to apply the new hostname." : "Saved.", restart);
  if (restart) BaseCore::scheduleRestart(1500);
}

void handleForget() {
  if (!BaseCore::authorize()) return;
  forgetWifi();
  sendResult(true, "WiFi credentials erased. Restarting into setup AP \"" + g_apSsid + "\".", true);
  BaseCore::scheduleRestart(1500);
}

void handleReboot() {
  if (!BaseCore::authorize()) return;
  sendResult(true, "Restarting.", true);
  BaseCore::scheduleRestart(1000);
}

// Streams the uploaded .bin straight into the inactive OTA partition.
void handleUpdateUpload() {
  HTTPUpload& up = g_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    g_updateStarted = true;
    g_updateFirstChunk = true;
    g_updateError = "";
    g_updateAuthOk = g_admin.isEmpty() || g_server.authenticate(ADMIN_USER, g_admin.c_str());
    if (!g_updateAuthOk) {
      g_updateError = "Unauthorized";
      return;
    }
    Serial.printf("[ota] receiving %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) g_updateError = Update.errorString();
    return;
  }

  if (!g_updateAuthOk || !g_updateError.isEmpty()) {
    if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
      if (Update.isRunning()) Update.abort();
    }
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    if (g_updateFirstChunk) {
      g_updateFirstChunk = false;
      if (up.currentSize < 16 || up.buf[0] != 0xE9) {
        g_updateError = "Not an ESP32 application image (.bin).";
        Update.abort();
        return;
      }
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
      uint16_t chipId = up.buf[12] | (up.buf[13] << 8);
      if (chipId != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        g_updateError = "Image was built for a different chip (expected " CONFIG_IDF_TARGET ").";
        Update.abort();
        return;
      }
#endif
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      g_updateError = Update.errorString();
      if (Update.isRunning()) Update.abort();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      g_updateError = Update.errorString();
    } else {
      Serial.printf("[ota] success, %u bytes\n", (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.abort();
    g_updateError = "Upload aborted.";
  }
}

void handleUpdateDone() {
  g_server.sendHeader("Connection", "close");
  if (!g_updateStarted) {
    if (!BaseCore::authorize()) return;
    return sendResult(false, "No firmware file received.");
  }
  g_updateStarted = false;
  if (!g_updateAuthOk) {
    g_server.requestAuthentication();
    return;
  }
  if (!g_updateError.isEmpty()) {
    Serial.printf("[ota] failed: %s\n", g_updateError.c_str());
    return sendResult(false, "Update failed: " + g_updateError);
  }
  sendResult(true, "Update successful. Restarting into the new firmware.", true);
  BaseCore::scheduleRestart(1500);
}

void handleNotFound() {
  if (captiveRedirect()) return;
  g_server.send(404, "text/plain", "Not found");
}

void registerRoutes() {
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/scan", HTTP_GET, handleScan);
  g_server.on("/api/wifi", HTTP_POST, handleWifiSave);
  g_server.on("/api/device", HTTP_POST, handleDeviceSave);
  g_server.on("/api/forget", HTTP_POST, handleForget);
  g_server.on("/api/reboot", HTTP_POST, handleReboot);
  g_server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  g_server.onNotFound(handleNotFound);
}

}  // namespace

// ------------------------------------------------------------- public API --

namespace BaseCore {

void begin(const BaseConfig& cfg) {
  g_cfg = cfg;
  if (g_cfg.resetButtonPin >= 0) pinMode(g_cfg.resetButtonPin, INPUT_PULLUP);

  // A freshly uploaded image boots once "pending verify" when rollback is
  // enabled; we got this far, so mark it good.
  esp_ota_mark_app_valid_cancel_rollback();

  loadSettings();
  Serial.printf("\n%s v%s (%s)\n", g_cfg.title, g_cfg.fwVersion, g_host.c_str());

  WiFi.persistent(false);  // credentials live in our own Preferences namespace
  if (!connectStation()) startPortal();

  registerRoutes();
  g_server.begin();
}

void loop() {
  if (g_portal) {
    g_dns.processNextRequest();
    portalRetryTick();
  } else {
    stationWatchTick();
  }
  g_server.handleClient();
  resetButtonTick();

  if (g_restartPending && (int32_t)(millis() - g_restartAt) >= 0) {
    Serial.println("[sys] restarting");
    delay(100);
    ESP.restart();
  }
}

WebServer& server() { return g_server; }

bool authorize() {
  if (g_admin.isEmpty() || g_server.authenticate(ADMIN_USER, g_admin.c_str())) return true;
  g_server.requestAuthentication(BASIC_AUTH, g_cfg.title);
  return false;
}

bool isPortalMode() { return g_portal; }
bool isConnected() { return WiFi.status() == WL_CONNECTED; }
String hostname() { return g_host; }

void scheduleRestart(uint32_t delayMs) {
  g_restartPending = true;
  g_restartAt = millis() + delayMs;
}

}  // namespace BaseCore
