// HTTP API for the BRMesh page (all routes honour the BaseCore admin password).
//
//   GET  /api/mesh           lights, discovered lights, settings, radio + MQTT status, log
//   POST /api/mesh/scan      ask lights in pairing mode to announce themselves
//   POST /api/mesh/add       did (from a scan), addr, name
//   POST /api/mesh/edit      uid, name, addr, type
//   POST /api/mesh/bind      uid
//   POST /api/mesh/remove    uid
//   POST /api/mesh/control   uid, on, bri (1-255), rgb (rrggbb), mode (rgb|white|ct), mireds
//   POST /api/mesh/all       on
//   POST /api/mesh/settings  key, txms, autoadd
//   POST /api/mesh/mqtt      enabled, host, port, user, pass, prefix

#pragma once

namespace MeshWeb {

void begin();

}  // namespace MeshWeb
