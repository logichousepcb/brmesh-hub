#include "MeshRadio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>

// Keep the BLE controller memory: Arduino releases it at boot unless a BLE
// library claims it, and we drive NimBLE without the Arduino BLE library.
extern "C" bool bleInUse(void) { return true; }

namespace MeshRadio {

namespace {

// Scan 50% of the time so WiFi keeps its share of the shared 2.4 GHz radio.
constexpr uint16_t SCAN_ITVL = 0x60;    // 60 ms
constexpr uint16_t SCAN_WINDOW = 0x30;  // 30 ms
constexpr uint16_t ADV_ITVL_MIN = 0x20; // 20 ms
constexpr uint16_t ADV_ITVL_MAX = 0x30; // 30 ms
constexpr uint32_t DISC_RETRY_MS = 2000;

QueueHandle_t g_rxQueue = nullptr;
volatile bool g_synced = false;
uint8_t g_ownAddrType = BLE_OWN_ADDR_PUBLIC;

bool g_advOn = false;
uint32_t g_advStart = 0;
uint32_t g_advDuration = 0;
uint32_t g_lastDiscTry = 0;

volatile uint32_t g_rx = 0;
volatile uint32_t g_dropped = 0;
uint32_t g_tx = 0;

int onGapEvent(struct ble_gap_event* event, void* arg);

void startDiscovery() {
  if (!g_synced || ble_gap_disc_active()) return;
  struct ble_gap_disc_params p = {};
  p.itvl = SCAN_ITVL;
  p.window = SCAN_WINDOW;
  p.filter_policy = 0;
  p.limited = 0;
  p.passive = 1;
  // Every light advertises from the same address, so the controller's
  // per-address duplicate filter would hide almost everything.
  p.filter_duplicates = 0;
  int rc = ble_gap_disc(g_ownAddrType, BLE_HS_FOREVER, &p, onGapEvent, nullptr);
  if (rc != 0) Serial.printf("[ble] scan start failed: %d\n", rc);
}

// Called in the NimBLE host task: find manufacturer data 0xFFF0 and queue it.
void onAdvReport(const struct ble_gap_disc_desc& d) {
  const uint8_t* p = d.data;
  uint8_t n = d.length_data;
  for (uint8_t i = 0; i + 1 < n;) {
    uint8_t len = p[i];
    if (len == 0 || i + 1 + len > n) break;
    uint8_t type = p[i + 1];
    const uint8_t* v = p + i + 2;
    uint8_t vlen = len - 1;
    if (type == 0xFF && vlen >= 3 && v[0] == (fastcon::kManufacturerId & 0xFF) &&
        v[1] == (fastcon::kManufacturerId >> 8)) {
      RxPacket pkt;
      for (int k = 0; k < 6; k++) pkt.mac[k] = d.addr.val[5 - k];
      pkt.rssi = d.rssi;
      pkt.len = vlen - 2 > sizeof(pkt.data) ? sizeof(pkt.data) : vlen - 2;
      memcpy(pkt.data, v + 2, pkt.len);
      g_rx = g_rx + 1;
      if (xQueueSend(g_rxQueue, &pkt, 0) != pdTRUE) g_dropped = g_dropped + 1;
      return;
    }
    i += 1 + len;
  }
}

int onGapEvent(struct ble_gap_event* event, void*) {
  switch (event->type) {
    case BLE_GAP_EVENT_DISC:
      onAdvReport(event->disc);
      break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
      // Restarted from loop().
      break;
    default:
      break;
  }
  return 0;
}

void onSync() {
  ble_hs_util_ensure_addr(0);
  if (ble_hs_id_infer_auto(0, &g_ownAddrType) != 0) g_ownAddrType = BLE_OWN_ADDR_PUBLIC;
  g_synced = true;
  Serial.println("[ble] host synced");
  startDiscovery();
}

void onReset(int reason) {
  g_synced = false;
  Serial.printf("[ble] host reset, reason %d\n", reason);
}

void hostTask(void*) {
  nimble_port_run();  // returns only after nimble_port_stop()
  nimble_port_freertos_deinit();
}

void stopAdvertising() {
  if (ble_gap_adv_active()) ble_gap_adv_stop();
  g_advOn = false;
}

}  // namespace

bool begin() {
  g_rxQueue = xQueueCreate(48, sizeof(RxPacket));
  if (!g_rxQueue) return false;

  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    Serial.printf("[ble] nimble_port_init failed: %d\n", err);
    return false;
  }
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  nimble_port_freertos_init(hostTask);

  uint32_t start = millis();
  while (!g_synced && millis() - start < 3000) delay(10);
  if (!g_synced) Serial.println("[ble] host did not sync yet");
  return true;
}

void loop() {
  if (!g_synced) return;
  if (g_advOn && millis() - g_advStart >= g_advDuration) stopAdvertising();
  if (!ble_gap_disc_active() && millis() - g_lastDiscTry >= DISC_RETRY_MS) {
    g_lastDiscTry = millis();
    startDiscovery();
  }
}

bool ready() { return g_synced; }

bool busy() { return g_advOn; }

bool broadcast(const fastcon::Frame& f, uint32_t durationMs) {
  if (!g_synced) return false;
  stopAdvertising();

  // Flags, then manufacturer data: length, 0xFF, company ID (LE), frame.
  uint8_t raw[31];
  size_t n = 0;
  raw[n++] = 0x02;
  raw[n++] = 0x01;
  raw[n++] = 0x02;
  raw[n++] = (uint8_t)(3 + f.len);
  raw[n++] = 0xFF;
  raw[n++] = fastcon::kManufacturerId & 0xFF;
  raw[n++] = fastcon::kManufacturerId >> 8;
  memcpy(raw + n, f.data, f.len);
  n += f.len;

  int rc = ble_gap_adv_set_data(raw, n);
  if (rc != 0) {
    Serial.printf("[ble] adv data failed: %d\n", rc);
    return false;
  }
  struct ble_gap_adv_params p = {};
  p.conn_mode = BLE_GAP_CONN_MODE_NON;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  p.itvl_min = ADV_ITVL_MIN;
  p.itvl_max = ADV_ITVL_MAX;
  rc = ble_gap_adv_start(g_ownAddrType, nullptr, BLE_HS_FOREVER, &p, onGapEvent, nullptr);
  if (rc != 0) {
    Serial.printf("[ble] adv start failed: %d\n", rc);
    return false;
  }
  g_advOn = true;
  g_advStart = millis();
  g_advDuration = durationMs;
  g_tx++;
  return true;
}

bool receive(RxPacket& out) {
  return g_rxQueue && xQueueReceive(g_rxQueue, &out, 0) == pdTRUE;
}

uint32_t rxCount() { return g_rx; }
uint32_t txCount() { return g_tx; }
uint32_t rxDropped() { return g_dropped; }

}  // namespace MeshRadio
