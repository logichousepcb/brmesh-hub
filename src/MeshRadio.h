// BLE radio for the Fastcon protocol, using the NimBLE host directly:
// a continuous passive scan for 0xFFF0 manufacturer data, plus one
// non-connectable advertisement at a time for outgoing frames.

#pragma once

#include <Arduino.h>

#include "Fastcon.h"

namespace MeshRadio {

struct RxPacket {
  uint8_t mac[6];  // display order (MSB first)
  int8_t rssi;
  uint8_t len;
  uint8_t data[29];  // manufacturer data after the company ID
};

bool begin();
void loop();

bool ready();
// Broadcast a frame for durationMs, replacing anything still on air.
bool broadcast(const fastcon::Frame& f, uint32_t durationMs);
bool busy();

// Received BRMesh frames, queued from the BLE task. Call from loop().
bool receive(RxPacket& out);

uint32_t rxCount();
uint32_t txCount();
uint32_t rxDropped();

}  // namespace MeshRadio
