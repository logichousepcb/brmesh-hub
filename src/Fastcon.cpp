#include "Fastcon.h"

#include <string.h>

namespace fastcon {

namespace {

const uint8_t DEFAULT_KEY[4] = {0x5E, 0x36, 0x7B, 0xC4};  // header key / unbound devices
const uint8_t FASTCON_ADDR[3] = {0xC1, 0xC2, 0xC3};
const uint8_t RF_PREAMBLE[3] = {0x71, 0x0F, 0x55};
constexpr uint8_t WHITENING_SEED = 0x25;
constexpr size_t RF_SKIP = 0x0F;  // leading bytes of the RF buffer that aren't sent

uint8_t reverse8(uint8_t v) {
  uint8_t r = 0;
  for (int i = 0; i < 8; i++) r |= ((v >> i) & 1) << (7 - i);
  return r;
}

// CRC-16/X-25 over address + data: what real lights put on frames they relay.
uint16_t crc16(const uint8_t* addr, size_t addrLen, const uint8_t* data, size_t dataLen) {
  uint16_t crc = 0xFFFF;
  auto feed = [&crc](uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
  };
  for (size_t i = 0; i < addrLen; i++) feed(addr[i]);
  for (size_t i = 0; i < dataLen; i++) feed(data[i]);
  return crc ^ 0xFFFF;
}

// 7-bit LFSR whitening, in place. It is its own inverse.
void whiten(uint8_t* data, size_t len) {
  uint32_t f0 = 1, f4 = (WHITENING_SEED >> 5) & 1, f8 = (WHITENING_SEED >> 4) & 1,
           fc = (WHITENING_SEED >> 3) & 1, f10 = (WHITENING_SEED >> 2) & 1,
           f14 = (WHITENING_SEED >> 1) & 1, f18 = WHITENING_SEED & 1;
  for (size_t i = 0; i < len; i++) {
    uint32_t vc = fc, v14 = f14, v18 = f18, v10 = f10;
    uint32_t v8 = v14 ^ f8;
    uint32_t v4 = v10 ^ f4;
    uint32_t v_ = v18 ^ vc;
    uint32_t v0 = v_ ^ f0;
    uint8_t c = data[i];
    data[i] = (uint8_t)(((c & 0x80) ^ ((v8 ^ v18) << 7)) + ((c & 0x40) ^ (v0 << 6)) +
                        ((c & 0x20) ^ (v4 << 5)) + ((c & 0x10) ^ (v8 << 4)) +
                        ((c & 0x08) ^ (v_ << 3)) + ((c & 0x04) ^ (v10 << 2)) +
                        ((c & 0x02) ^ (v14 << 1)) + ((c & 0x01) ^ (v18 << 0)));
    f8 = v4;
    fc = v8;
    f10 = v8 ^ vc;
    f14 = v0 ^ v10;
    f18 = v4 ^ v14;
    f0 = v8 ^ v18;
    f4 = v0;
  }
}

void decryptHeader(const uint8_t* p, uint8_t h[4]) {
  for (int i = 0; i < 4; i++) h[i] = p[i] ^ DEFAULT_KEY[i];
}

uint8_t bodyChecksum(const uint8_t h[4], const uint8_t* content, size_t len) {
  uint8_t sum = h[0] + h[1] + h[2];
  for (size_t i = 0; i < len; i++) sum += content[i];
  return sum;
}

}  // namespace

// ------------------------------------------------------------ device types --

uint8_t capabilities(uint16_t type) {
  switch (type) {
    case TYPE_LIGHT_RGB: return CAP_DIM | CAP_RGB;
    case TYPE_LIGHT_RGBW: return CAP_DIM | CAP_RGB | CAP_WHITE;
    case TYPE_LIGHT_RGBCW: return CAP_DIM | CAP_RGB | CAP_CT;
    case TYPE_LIGHT_CCT:
    case TYPE_LIGHT_W_CW:
    case TYPE_LIGHT_BURDEN_CW: return CAP_DIM | CAP_CT;
    case TYPE_LIGHT_PWR: return 0;
    case TYPE_GENERIC: return CAP_DIM | CAP_RGB | CAP_WHITE | CAP_CT;
    default: return CAP_DIM;
  }
}

const char* typeName(uint16_t type) {
  switch (type) {
    case TYPE_GENERIC: return "Generic";
    case TYPE_LIGHT_PWR: return "On/Off";
    case TYPE_LIGHT_RGBCW: return "RGBCW";
    case TYPE_LIGHT_CCT: return "CCT";
    case TYPE_LIGHT_RGB: return "RGB";
    case TYPE_LIGHT_RGBW: return "RGBW";
    case TYPE_LIGHT_COMPOSE: return "Compose";
    case TYPE_LIGHT_W_CW: return "W/CW";
    case TYPE_LIGHT_BURDEN_CW: return "Burden CW";
    case TYPE_LIGHT_BURDEN_W: return "Burden W";
    case 43499: return "Curtain";
    case 43531: return "Fan";
    case 43500: case 43756: case 10058: return "Gateway";
    case 43459: case 43461: case 43462: case 43463: case 43472: case 43473: case 43733:
    case 43734: return "Panel";
    case 43474: case 43525: case 43680: return "Relay";
    case 43505: case 43516: case 43791: case 43808: return "Sensor";
    case 43919: return "Thermostat";
    default: return "Unknown";
  }
}

bool isLight(uint16_t type) {
  switch (type) {
    case TYPE_GENERIC: case TYPE_LIGHT_PWR: case TYPE_LIGHT_RGBCW: case TYPE_LIGHT_CCT:
    case TYPE_LIGHT_RGB: case TYPE_LIGHT_RGBW: case TYPE_LIGHT_COMPOSE: case TYPE_LIGHT_W_CW:
    case TYPE_LIGHT_BURDEN_CW: case TYPE_LIGHT_BURDEN_W: return true;
    default: return false;
  }
}

// ------------------------------------------------------------------ builder --

uint8_t Builder::nextSequence() {
  seq_++;
  if (seq_ == 0) seq_ = 1;
  return seq_;
}

Frame Builder::generate(uint8_t type, const uint8_t* data, size_t len, const uint8_t* key,
                        bool forward, uint8_t addrHigh) {
  // Body: header (control, sequence, safe key, checksum) + data, encrypted.
  uint8_t body[16] = {0};
  if (len > 12) len = 12;
  body[0] = (addrHigh & 0x0F) | ((type & 0x07) << 4) | (forward ? 0x80 : 0);
  body[1] = nextSequence();
  body[2] = key ? key[3] : 0xFF;
  memcpy(body + 4, data, len);
  body[3] = bodyChecksum(body, body + 4, 12);
  for (int i = 0; i < 4; i++) body[i] ^= DEFAULT_KEY[i];
  const uint8_t* k = key ? key : DEFAULT_KEY;
  for (int i = 0; i < 12; i++) body[4 + i] ^= k[i & 3];

  // RF frame: [15 unsent] preamble(3) address(3, reversed) body(16) crc(2).
  uint8_t rf[RF_SKIP + 3 + 3 + 16 + 2] = {0};
  memcpy(rf + RF_SKIP, RF_PREAMBLE, 3);
  for (int i = 0; i < 3; i++) rf[RF_SKIP + 3 + 2 - i] = FASTCON_ADDR[i];
  memcpy(rf + RF_SKIP + 6, body, 16);
  for (size_t i = RF_SKIP; i < RF_SKIP + 6; i++) rf[i] = reverse8(rf[i]);
  uint16_t crc = crc16(FASTCON_ADDR, 3, body, 16);
  rf[RF_SKIP + 22] = crc & 0xFF;
  rf[RF_SKIP + 23] = crc >> 8;
  whiten(rf, sizeof(rf));

  Frame f;
  f.len = kFrameLen;
  memcpy(f.data, rf + RF_SKIP, kFrameLen);
  return f;
}

Frame Builder::scan(bool forward) {
  uint8_t data[12] = {0};
  return generate(CMD_SCAN, data, 12, nullptr, forward, 0);
}

Frame Builder::bind(const uint8_t did[6], uint8_t meshAddr, const uint8_t meshKey[4],
                    const uint8_t deviceKey[4]) {
  uint8_t data[12];
  memcpy(data, did, 6);
  data[6] = meshAddr;
  data[7] = 0x01;
  memcpy(data + 8, meshKey, 4);
  return generate(CMD_BIND, data, 12, deviceKey, false, 0);
}

Frame Builder::light(uint16_t meshAddr, const uint8_t meshKey[4], const uint8_t* cmd,
                     size_t cmdLen) {
  uint8_t data[12] = {0};
  if (cmdLen > 10) cmdLen = 10;
  data[0] = (uint8_t)(2 | ((cmdLen + 1) << 4));
  data[1] = meshAddr & 0xFF;
  memcpy(data + 2, cmd, cmdLen);
  return generate(CMD_CONTROL, data, 12, meshKey, true, meshAddr >> 8);
}

// --------------------------------------------------------- light payloads --

size_t lightOff(uint8_t out[6]) {
  out[0] = 0;
  return 1;
}

size_t lightOn(uint8_t out[6], uint8_t level) {
  out[0] = 0x80 | (level & 0x7F);
  return 1;
}

// The app normalises the colour so the channels add up to 255; brightness is
// carried separately in byte 0.
size_t lightRgb(uint8_t out[6], uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  unsigned total = (unsigned)r + g + b;
  auto norm = [total](uint8_t c) -> uint8_t {
    if (!total) return 0;
    unsigned v = (unsigned)c * 255u / total;
    return v > 255 ? 255 : (uint8_t)v;
  };
  out[0] = 0x80 | (level & 0x7F);
  out[1] = norm(b);
  out[2] = norm(r);
  out[3] = norm(g);
  out[4] = 0;
  out[5] = 0;
  return 6;
}

size_t lightWhite(uint8_t out[6], uint8_t level) {
  return lightColdWarm(out, 127, 127, level);
}

size_t lightColdWarm(uint8_t out[6], uint8_t warm, uint8_t cold, uint8_t level) {
  out[0] = 0x80 | (level & 0x7F);
  out[1] = out[2] = out[3] = 0;
  out[4] = warm;
  out[5] = cold;
  return 6;
}

// ------------------------------------------------------------------ parsing --

bool unwrapRfFrame(const uint8_t* p, size_t n, uint8_t body[16]) {
  if (n != kFrameLen) return false;
  uint8_t buf[RF_SKIP + kFrameLen] = {0};
  memcpy(buf + RF_SKIP, p, n);
  whiten(buf, sizeof(buf));
  const uint8_t* frame = buf + RF_SKIP;
  for (int i = 0; i < 3; i++) {
    if (reverse8(frame[i]) != RF_PREAMBLE[i]) return false;
  }
  memcpy(body, frame + 6, 16);
  return true;
}

int frameType(const uint8_t* p, size_t n) {
  uint8_t body[16];
  if (unwrapRfFrame(p, n, body)) {
    p = body;
    n = 16;
  }
  if (n < 4) return -1;
  return ((p[0] ^ DEFAULT_KEY[0]) >> 4) & 7;
}

// Discovery broadcast (plain, 16 bytes):
//   header(4, default key) | DID(6) | type(2, LE) | key(4)
// Decrypted header byte 2 is the address the light currently has, byte 3 the
// sum of the other header bytes and the raw payload.
bool parseDiscovery(const uint8_t* p, size_t n, Discovery& out) {
  if (n != 16 && n != 18) return false;
  // Some captures show the body 2 bytes in; prefer the offset whose checksum fits.
  int chosen = -1;
  for (size_t off = 0; off + 16 <= n; off += 2) {
    uint8_t h[4];
    decryptHeader(p + off, h);
    if (((h[0] >> 4) & 7) != CMD_DISCOVERY) continue;
    if (chosen < 0) chosen = (int)off;
    if (bodyChecksum(h, p + off + 4, 12) == h[3]) {
      chosen = (int)off;
      break;
    }
  }
  if (chosen < 0) return false;
  const uint8_t* b = p + chosen;
  memcpy(out.did, b + 4, 6);
  out.type = b[10] | (b[11] << 8);
  memcpy(out.key, b + 12, 4);
  out.reportedAddr = b[2] ^ DEFAULT_KEY[2];
  return true;
}

// Heartbeat: header type 3, data encrypted with the mesh key, data byte 0
// subtype 4, byte 1 the address (high nibble in header byte 0), byte 2 the group.
bool parseHeartbeat(const uint8_t* p, size_t n, const uint8_t meshKey[4], Heartbeat& out) {
  uint8_t body[16];
  if (unwrapRfFrame(p, n, body)) {
    p = body;
    n = 16;
  }
  if (n < 16 || n > 24) return false;
  uint8_t h[4];
  decryptHeader(p, h);
  if (((h[0] >> 4) & 7) != CMD_UPLOAD) return false;
  uint8_t content[20];
  size_t len = n - 4;
  for (size_t i = 0; i < len; i++) content[i] = p[4 + i] ^ meshKey[i & 3];
  if (bodyChecksum(h, content, len) != h[3]) return false;  // other mesh key
  if ((content[0] & 0x0F) != 4) return false;
  out.addr = content[1] | ((h[0] & 0x0F) << 8);
  out.group = content[2];
  return true;
}

}  // namespace fastcon
