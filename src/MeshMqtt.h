// Optional MQTT link to Home Assistant: publishes MQTT discovery for every
// light (JSON schema) plus a "Scan for lights" button, and applies commands.
//
// Topics (node = "brmesh_" + MAC):
//   brmesh/<node>/status             online / offline (retained, LWT)
//   brmesh/<node>/light/<name>/set    commands from Home Assistant
//   brmesh/<node>/light/<name>/state  assumed state (retained)
//
// <name> is the light's name as a topic (MeshHub::topicName), so it moves
// when the light is renamed. Discovery configs and unique IDs use the
// device ID, so Home Assistant keeps the same entity.
//   brmesh/<node>/scan               press = scan for lights in pairing mode

#pragma once

#include <Arduino.h>

#include "MeshHub.h"

namespace MeshMqtt {

void begin(const char* fwVersion);
void loop();
bool connected();
String nodeId();
// Base topic of a light: brmesh/<node>/light/<name>
String lightTopic(const MeshHub::Light& l);

}  // namespace MeshMqtt
