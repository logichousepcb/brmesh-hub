# BRMesh Hub REST API (v1)

A JSON API for controlling lights from scripts, home-automation systems and other tools. It covers
everything the web page's **Home** tab does, plus the mesh settings.

- Base URL: `http://<hub>/api/v1`, where `<hub>` is `brmesh-hub-xxxx.local` or the hub's IP address.
- Request bodies are JSON (`Content-Type: application/json`). Responses are JSON.
- Plain HTTP only, port 80.

## Authentication

If an admin password is set (**Settings → Device Settings**), every request needs HTTP Basic auth
with user `admin`. Without a password the API is open to anyone on the network.

```
curl -u admin:yourpassword http://brmesh-hub-xxxx.local/api/v1/lights
```

Requests without valid credentials get `401 Unauthorized`.

## Errors

Errors return a 4xx status and a message:

```json
{"error": "No light with id, name or address \"porch\"."}
```

| Status | Meaning |
| --- | --- |
| 400 | Invalid JSON, an unknown field, or a value out of range |
| 401 | Admin password required |
| 404 | No such light, or an unsupported method on that path |
| 405 | `GET /lights/all/state`: use `PUT` |
| 409 | The address or name (MQTT topic) is already used, or the light is already added |

## Endpoints

| Method | Path | Does |
| --- | --- | --- |
| GET | `/api/v1` | Hub info and status |
| GET | `/api/v1/lights` | List lights |
| POST | `/api/v1/lights` | Add a discovered light |
| GET | `/api/v1/lights/{light}` | One light |
| PUT, PATCH | `/api/v1/lights/{light}` | Rename or re-address a light |
| DELETE | `/api/v1/lights/{light}` | Remove a light |
| GET | `/api/v1/lights/{light}/state` | A light's state |
| PUT, POST | `/api/v1/lights/{light}/state` | Change a light's state |
| PUT, POST | `/api/v1/lights/all/state` | Change every light's state |
| POST | `/api/v1/lights/{light}/bind` | Send the bind again |
| POST | `/api/v1/scan` | Scan for lights in pairing mode |
| GET | `/api/v1/discovered` | Lights found by scans |
| GET | `/api/v1/settings` | Mesh settings |
| PUT, PATCH | `/api/v1/settings` | Change mesh settings |
| GET | `/api/v1/log` | Recent activity log |

### Identifying a light

`{light}` can be any of these, tried in this order:

1. The light's `id`, for example `A4C1380B12F0`.
2. Its name, not case-sensitive and URL-encoded: `Porch%20Light`.
3. Its mesh address, for example `3`.

`all` is reserved for `/lights/all/state`.

## Light object

```json
{
  "id": "A4C1380B12F0",
  "name": "Porch Light",
  "address": 3,
  "type": 43050,
  "type_name": "RGBCW",
  "device_id": "A4C1380B12F0",
  "capabilities": ["brightness", "rgb", "color_temp"],
  "state": {
    "on": true,
    "brightness": 200,
    "color_mode": "color_temp",
    "rgb": [255, 180, 90],
    "color_temp": 300,
    "color_temp_kelvin": 3333
  },
  "last_heard_s": 12,
  "bind_pending": false,
  "mqtt_topic": "brmesh/brmesh_xxxxxx/light/porch_light"
}
```

| Field | Notes |
| --- | --- |
| `id` | Stable ID (the device ID). It doesn't change on rename or re-address |
| `address` | Mesh address, 1-255 |
| `capabilities` | Which state fields the light uses. A light with none is on/off only |
| `state` | The hub's **assumed** state. Lights don't report their state, so this is what the hub last sent |
| `state.color_mode` | `onoff`, `brightness`, `rgb`, `white` or `color_temp` |
| `last_heard_s` | Seconds since the light's last heartbeat, or `null` if never heard |
| `bind_pending` | `true` while the hub waits for the light to confirm a bind |
| `mqtt_topic` | The light's MQTT base topic, built from its name. Commands go to `<mqtt_topic>/set` and state is on `<mqtt_topic>/state` |

## Hub info

`GET /api/v1`

```json
{
  "name": "BRMesh Hub",
  "version": "1.2.0",
  "api": 1,
  "chip": "esp32c6",
  "hostname": "brmesh-hub-xxxx",
  "uptime_s": 3605,
  "lights": 4,
  "scanning": false,
  "radio": {"ready": true, "received": 1520, "sent": 87, "dropped": 0, "queued": 0},
  "mqtt": {"enabled": true, "connected": true, "node": "brmesh_xxxxxx"}
}
```

## Controlling lights

`PUT /api/v1/lights/{light}/state` with any of these fields:

| Field | Value |
| --- | --- |
| `on` | `true`, `false` or `"toggle"` |
| `brightness` | 0-255. `0` turns the light off; any other value turns it on |
| `rgb` | `[r, g, b]` (0-255 each) or `"#rrggbb"`. Switches to RGB mode |
| `color_temp` | 153 (cool) to 500 (warm) mireds. Switches to colour temperature mode |
| `color_temp_kelvin` | 2000-6535 K, the same as `color_temp` |
| `color_mode` | `"rgb"`, `"white"` or `"color_temp"`. Switches mode with the stored values |

Setting a brightness, colour or mode turns the light on unless the request also sends `"on": false`.
Fields the light can't use (see `capabilities`) are ignored, so check the state in the response. It
returns the new state:

```json
{"on": true, "brightness": 128, "color_mode": "rgb", "rgb": [255, 0, 0], "color_temp": 300, "color_temp_kelvin": 3333}
```

The command is queued and broadcast for **Broadcast time per command** (300 ms by default). Lights
don't acknowledge commands. If several commands for the same light queue up, only the latest is sent.

`PUT /api/v1/lights/all/state` takes the same fields, except `"toggle"`, and returns
`[{"id": "...", "state": {...}}, ...]`.

## Adding lights

1. Power-cycle the light (off for 5 s, then on) to put it in pairing mode.
2. `POST /api/v1/scan`. It returns `202 {"scanning": true}`. Lights answer within a few seconds.
3. `GET /api/v1/discovered`:

   ```json
   [{"device_id": "A4C1380B12F0", "type": 43050, "type_name": "RGBCW", "is_light": true,
     "address": null, "rssi": -61, "last_seen_s": 3, "added": false}]
   ```

   `address` is the address the light reports, or `null` if it isn't bound.
4. `POST /api/v1/lights` with `{"device_id": "A4C1380B12F0", "name": "Porch Light", "address": 3}`.
   `name` and `address` are optional. The name is also the light's MQTT topic (see below); without
   one the light is called `Light XXXX` after its device ID. Without an address, the hub keeps the light's own address if
   it is free, or else picks the next free one. Returns `201` and the light, with
   `"bind_pending": true` until the light confirms.

If the bind doesn't take, power-cycle the light again, or call `POST /api/v1/lights/{light}/bind`
while it is in pairing mode. That returns `202` and the light.

## Editing and removing

`PUT /api/v1/lights/{light}` with `{"name": "Back Porch"}` and/or `{"address": 7}`. A new address
re-binds the light (`bind_pending` becomes `true`). Returns the light.

**Names and MQTT topics.** A light's MQTT topic comes from its name: lower case, with every run of
other characters turned into `_`. `Back Porch` becomes `brmesh/<node>/light/back_porch`. Renaming a
light moves its topics; the Home Assistant entity stays the same because its unique ID is the device
ID. Two lights can't have names that give the same topic (`Back Porch` and `back-porch`): the request
fails with `409`. A name also needs at least one letter or digit.

`DELETE /api/v1/lights/{light}` returns `{"deleted": "<id>"}`. The light keeps its mesh address
and key, because the protocol has no unbind command.

## Settings

`GET /api/v1/settings`

```json
{"mesh_key": "32323035", "tx_ms": 300, "auto_add": false}
```

`PUT /api/v1/settings` with any of these fields. Returns the settings.

| Field | Value |
| --- | --- |
| `mesh_key` | 8 hex characters. Changing it doesn't re-key lights that are already bound |
| `tx_ms` | 50-3000, how long each command is broadcast |
| `auto_add` | `true` to add and bind every light a scan finds |

MQTT, WiFi and device settings stay on the web page's **Settings** tab.

## Log

`GET /api/v1/log` returns the recent activity log as an array of strings, oldest first.

## Examples

```sh
HUB=http://brmesh-hub-xxxx.local
AUTH="-u admin:yourpassword"   # leave empty if no admin password is set

# List lights
curl $AUTH $HUB/api/v1/lights

# Turn a light on at half brightness
curl $AUTH -X PUT -H 'Content-Type: application/json' \
  -d '{"on": true, "brightness": 128}' "$HUB/api/v1/lights/Porch%20Light/state"

# Red
curl $AUTH -X PUT -H 'Content-Type: application/json' -d '{"rgb": "#ff0000"}' $HUB/api/v1/lights/3/state

# Warm white
curl $AUTH -X PUT -H 'Content-Type: application/json' -d '{"color_temp_kelvin": 2700}' $HUB/api/v1/lights/3/state

# Toggle
curl $AUTH -X POST -H 'Content-Type: application/json' -d '{"on": "toggle"}' $HUB/api/v1/lights/3/state

# Everything off
curl $AUTH -X PUT -H 'Content-Type: application/json' -d '{"on": false}' $HUB/api/v1/lights/all/state
```

Python:

```python
import requests

hub = "http://brmesh-hub-xxxx.local/api/v1"
auth = ("admin", "yourpassword")  # or None

for light in requests.get(f"{hub}/lights", auth=auth).json():
    print(light["name"], light["state"]["on"])

requests.put(f"{hub}/lights/3/state", json={"on": True, "rgb": [0, 0, 255]}, auth=auth)
```

PowerShell:

```powershell
$hub = "http://brmesh-hub-xxxx.local/api/v1"
Invoke-RestMethod "$hub/lights"
Invoke-RestMethod "$hub/lights/3/state" -Method Put -ContentType application/json -Body '{"on": false}'
```

## Notes

- **Browsers on other sites:** the API sends no CORS headers, so web pages on other sites can't call
  it with JSON. Call it from a server, a script or Home Assistant instead.
- **Speed:** one frame is on air at a time. A command takes about `tx_ms` to go out, so changing
  10 lights at once takes about 10 × 300 ms.
- **Web page routes:** the web page uses separate form-encoded routes under `/api/mesh/*`. They
  may change between versions. Use `/api/v1` for integrations.
