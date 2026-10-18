// JSON REST API for scripts and other integrations: /api/v1/...
// See docs/API.md. All routes honour the BaseCore admin password (HTTP Basic,
// user "admin").

#pragma once

namespace MeshApi {

void begin(const char* fwVersion);

}  // namespace MeshApi
