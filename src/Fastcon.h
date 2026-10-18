// BRMesh / Broadlink "Fastcon" BLE protocol.
//
// Lights don't use GATT: everything is a 31-byte legacy advertisement with
// manufacturer data under company ID 0xFFF0. Outgoing commands are a 16-byte
// "body" (4-byte header + 12 data bytes, XOR-encrypted) wrapped in an RF frame
// (preamble, address, CRC) and whitened, which yields 24 bytes on air.
//
// Header byte 0 = forward(bit 7) | type(bits 4-6) | address-high(bits 0-3).
// Types: 0 scan, 1 discovery (light announces itself), 2 bind, 3 upload
// (heartbeat), 5 control.
//
// Based on the reverse-engineering by Moody (MoodyAPI), dennispg/esphome-fastcon
// and stromaa00/fastConLight. No Arduino dependencies, so it can be tested on
// a host (tools/fastcon_selftest.cpp).

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fastcon {

constexpr uint16_t kManufacturerId = 0xFFF0;
constexpr size_t kFrameLen = 24;  // whitened RF frame for a 16-byte body

enum CmdType : uint8_t {
  CMD_SCAN = 0,
  CMD_DISCOVERY = 1,
  CMD_BIND = 2,
  CMD_UPLOAD = 3,
  CMD_CONTROL = 5,
};

// Device type codes reported in discovery broadcasts.
enum DeviceType : uint16_t {
  TYPE_GENERIC = 0,  // unknown / added by hand: offer every colour mode
  TYPE_LIGHT_PWR = 43049,
  TYPE_LIGHT_RGBCW = 43050,
  TYPE_LIGHT_CCT = 43051,
  TYPE_LIGHT_RGB = 43168,
  TYPE_LIGHT_RGBW = 43169,
  TYPE_LIGHT_COMPOSE = 43709,
  TYPE_LIGHT_W_CW = 43745,
  TYPE_LIGHT_BURDEN_CW = 43754,
  TYPE_LIGHT_BURDEN_W = 43759,
};

// Colour capabilities of a device type.
enum Capability : uint8_t {
  CAP_DIM = 1,    // brightness (without it: on/off only)
  CAP_RGB = 2,
  CAP_WHITE = 4,  // dedicated white channel
  CAP_CT = 8,     // cold/warm white mix
};

uint8_t capabilities(uint16_t type);
const char* typeName(uint16_t type);
bool isLight(uint16_t type);

struct Frame {
  uint8_t len = 0;
  uint8_t data[kFrameLen];
};

// Builds outgoing frames and tracks the sequence number (lights drop a frame
// whose sequence they have just seen, so it must change with every command).
class Builder {
 public:
  void setSequence(uint8_t seq) { seq_ = seq; }

  // Ask lights in discovery mode to announce themselves.
  Frame scan(bool forward = false);
  // Discovery response: gives a light its mesh address and the mesh key.
  // Encrypted with the light's own key from its discovery broadcast.
  Frame bind(const uint8_t did[6], uint8_t meshAddr, const uint8_t meshKey[4],
             const uint8_t deviceKey[4]);
  // Single-light control command (cmd = 1 or 6 bytes, see light* below).
  Frame light(uint16_t meshAddr, const uint8_t meshKey[4], const uint8_t* cmd, size_t cmdLen);

 private:
  Frame generate(uint8_t type, const uint8_t* data, size_t len, const uint8_t* key,
                 bool forward, uint8_t addrHigh);
  uint8_t nextSequence();
  uint8_t seq_ = 1;
};

// Light command payloads. level is the protocol brightness 1-127.
size_t lightOff(uint8_t out[6]);
size_t lightOn(uint8_t out[6], uint8_t level);
size_t lightRgb(uint8_t out[6], uint8_t r, uint8_t g, uint8_t b, uint8_t level);
size_t lightWhite(uint8_t out[6], uint8_t level);
size_t lightColdWarm(uint8_t out[6], uint8_t warm, uint8_t cold, uint8_t level);

struct Discovery {
  uint8_t did[6];      // device ID (last 2 bytes = the short name the app shows)
  uint16_t type;
  uint8_t key[4];      // device key, needed to bind it
  uint8_t reportedAddr;  // mesh address it currently has (0 = unbound)
};

struct Heartbeat {
  uint16_t addr;
  uint8_t group;
};

// Parse manufacturer data (the bytes after the 0xFFF0 company ID).
bool parseDiscovery(const uint8_t* p, size_t n, Discovery& out);
// Heartbeats are encrypted with the mesh key; returns false for other keys.
bool parseHeartbeat(const uint8_t* p, size_t n, const uint8_t meshKey[4], Heartbeat& out);
// Recover the 16-byte body of a whitened 24-byte RF frame (relayed commands).
bool unwrapRfFrame(const uint8_t* p, size_t n, uint8_t body[16]);
// Header type (bits 4-6 of decrypted byte 0) of a plain or RF-wrapped frame, or -1.
int frameType(const uint8_t* p, size_t n);

}  // namespace fastcon
