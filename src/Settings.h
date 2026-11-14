#pragma once
#include <ArduinoJson.h>
#include "Shared.h"

// Validation and persistence (NVS namespace "shutter"). Keys stay compatible with 2.0.x.
void configToJson(JsonDocument &d, const CaptureConfig &c);
bool parseCaptureConfig(JsonVariantConst d, CaptureConfig &c, String &error);

bool parseReadyConfig(JsonVariantConst d, ReadyConfig &c, String &error);

void loadSettings();
