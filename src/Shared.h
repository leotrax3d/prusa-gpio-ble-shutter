#pragma once
// Shared state of the firmware. Everything the control task touches is guarded by `guard`.
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "Engine.h"
#include "LogBuffer.h"
#include "ReadyButton.h"

// GPIOs that are safe to use on a classic ESP32 DevKit (no strapping/flash/input-only pins).
constexpr uint8_t ALLOWED_PINS[] = {13, 14, 25, 26, 27, 32, 33};
constexpr bool pinAllowed(uint32_t pin) {
  for (uint8_t p : ALLOWED_PINS) if (p == pin) return true;
  return false;
}

struct NetworkConfig {
  String ap = "Prusa-Shutter", password, ssid, wifiPassword;
  String hostname = "prusa-shutter", bleName = "Prusa Shutter";
  uint8_t channel = 1;
};

struct TaskMetrics { uint32_t cycles = 0, lastUs = 0, maxUs = 0, maxGapMs = 0; };
struct DeviceInfo { uint32_t flashSize = 0, appSize = 0, otaSpace = 0, resetReason = 0; };

extern Preferences prefs;
extern SemaphoreHandle_t guard;
extern CaptureEngine engine;
extern ReadyButton readyButton;
extern NetworkConfig net;
extern String adminPassword;
extern LogBuffer eventLog;
extern TaskMetrics taskMetrics;
extern DeviceInfo deviceInfo;
extern bool updating, rebootRequired;
extern uint32_t rebootAt;

struct Lock {
  Lock() { xSemaphoreTake(guard, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(guard); }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

template <typename... Args>
inline void logEventf(const char *format, Args... args) { eventLog.append(millis(), format, args...); }
inline void logEvent(const char *text) { eventLog.append(millis(), "%s", text); }

inline void scheduleReboot() { rebootAt = millis() + 1500; }

// Control helpers (Control.cpp). Callers hold the lock.
void stopCapture();
void stopReady();
void configureReadyPins();
bool readyUses(uint8_t pin);
void startControlTask();
