// Host-side check of src/Fastcon.cpp against vectors produced by the
// fastConLight Python implementation (tested on real lights).
//
//   c++ -std=c++17 -I src tools/fastcon_selftest.cpp src/Fastcon.cpp -o /tmp/fc && /tmp/fc

#include <stdio.h>
#include <string.h>

#include "Fastcon.h"

using namespace fastcon;

static int failures = 0;

static void hex(const uint8_t* d, size_t n, char* out) {
  for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
  out[2 * n] = 0;
}

static void expectFrame(const char* name, const Frame& f, const char* want) {
  char got[64];
  hex(f.data, f.len, got);
  bool ok = strcmp(got, want) == 0;
  printf("%-6s %s %s\n", name, ok ? "ok  " : "FAIL", got);
  if (!ok) {
    printf("       want %s\n", want);
    failures++;
  }
}

static size_t unhex(const char* s, uint8_t* out) {
  size_t n = strlen(s) / 2;
  for (size_t i = 0; i < n; i++) sscanf(s + 2 * i, "%2hhx", &out[i]);
  return n;
}

static void check(const char* name, bool ok) {
  printf("%-6s %s\n", name, ok ? "ok" : "FAIL");
  if (!ok) failures++;
}

int main() {
  const uint8_t key[4] = {0x32, 0x32, 0x30, 0x35};
  const uint8_t did[6] = {0xEC, 0x0B, 0xF1, 0x0F, 0x00, 0xE9};
  const uint8_t dkey[4] = {0x5E, 0x36, 0x7B, 0xC4};
  uint8_t cmd[6];

  Builder b;
  b.setSequence(0x40);
  expectFrame("scan", b.scan(), "6db64368931d0d445c3ec63e5f0f65ca0a67aa63130b71dd");
  expectFrame("bind", b.bind(did, 7, key, dkey), "6db64368931d2d4767a92a35ae0065230d669851233ed69f");
  size_t n = lightOn(cmd, 100);
  expectFrame("on", b.light(3, key, cmd, n), "6db64368931ddd46962f8839f0fe09ce4196c66758fa67fd");
  n = lightRgb(cmd, 255, 40, 10, 64);
  expectFrame("rgb", b.light(3, key, cmd, n), "6db64368931ddd419602d839d4f6dcef4196c66758fa7639");
  n = lightWhite(cmd, 127);
  expectFrame("white", b.light(200, key, cmd, n), "6db64368931ddd4096ffd8f2ebfe09ce3ee9c66758fa83ad");
  n = lightOff(cmd);
  expectFrame("off", b.light(1, key, cmd, n), "6db64368931ddd439610883b14fe09ce4196c66758fa2903");

  // Heartbeats, plain and as a relayed RF frame.
  uint8_t buf[32];
  Heartbeat hb{};
  n = unhex("6e634e0e363139353232303532323035", buf);
  check("hb", parseHeartbeat(buf, n, key, hb) && hb.addr == 3 && hb.group == 9);
  n = unhex("6db64368931d3d5096b4ae391dfe09ce4196c66758fa4227", buf);
  hb = {};
  check("hb-rf", parseHeartbeat(buf, n, key, hb) && hb.addr == 3 && hb.group == 9);
  const uint8_t wrongKey[4] = {0xA1, 0xA2, 0xA3, 0xA4};
  check("hb-key", !parseHeartbeat(buf, n, wrongKey, hb));

  // Discovery broadcast captured by Moody.
  Discovery d{};
  n = unhex("4eb17a50ec0bf10f00e9a1a85e367bc4", buf);
  check("disc", parseDiscovery(buf, n, d) && memcmp(d.did, did, 6) == 0 &&
                    d.type == TYPE_LIGHT_RGBW && memcmp(d.key, dkey, 4) == 0 &&
                    d.reportedAddr == 1);
  check("type", frameType(buf, n) == CMD_DISCOVERY);

  printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
