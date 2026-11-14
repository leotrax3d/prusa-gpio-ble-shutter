// Prusa Shutter: Hackerboard OUT6 -> ESP32 GPIO -> Bluetooth "volume" key -> phone camera.
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#include "Ble.h"
#include "Settings.h"
#include "Shared.h"
#include "WebApi.h"

Preferences prefs;
SemaphoreHandle_t guard;
CaptureEngine engine;
ReadyButton readyButton;
NetworkConfig net;
String adminPassword;
LogBuffer eventLog;
TaskMetrics taskMetrics;
DeviceInfo deviceInfo;
bool updating = false, rebootRequired = false;
uint32_t rebootAt = 0;

static void startNetwork() {
  WiFi.mode(net.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.setHostname(net.hostname.c_str());
  WiFi.softAP(net.ap.c_str(), net.password.c_str(), net.channel, false, 4);
  if (net.ssid.length()) WiFi.begin(net.ssid.c_str(), net.wifiPassword.c_str());
  if (MDNS.begin(net.hostname.c_str())) MDNS.addService("http", "tcp", 80);
  Serial.printf("\nPrusa Shutter %s\nWi-Fi: %s\nWi-Fi password: %s\nDashboard: http://192.168.4.1\nLogin: admin / %s\n",
                FIRMWARE_VERSION, net.ap.c_str(), net.password.c_str(), adminPassword.c_str());
}

void setup() {
  Serial.begin(115200);
  guard = xSemaphoreCreateMutex();
  eventLog.seed(esp_random());
  loadSettings();

  // getSketchSize() verifies the whole image; read device facts once, not per status request.
  deviceInfo.flashSize = ESP.getFlashChipSize();
  deviceInfo.appSize = ESP.getSketchSize();
  deviceInfo.otaSpace = ESP.getFreeSketchSpace();
  deviceInfo.resetReason = uint32_t(esp_reset_reason());

  pinMode(engine.cfg.pin, engine.cfg.pullup ? INPUT_PULLUP : INPUT);
  engine.input.reset(digitalRead(engine.cfg.pin), millis());

  startNetwork();
  ble::begin(net.bleName.c_str());
  webapi::begin();
  {
    Lock lock;
    logEvent("Started. Capture off.");
  }
  startControlTask();
}

void loop() {
  webapi::handle();
  if (rebootAt && due(millis(), rebootAt)) ESP.restart();
  static uint32_t lastSerial = 0;
  if (millis() - lastSerial > 15000) {
    lastSerial = millis();
    Serial.printf("STATUS BLE=%d ready=%d IP=%s\n", ble::connected(), ble::ready(), WiFi.localIP().toString().c_str());
  }
  delay(2);
}
