// Local HTTP dashboard and JSON API. Handlers copy state under the lock and build JSON afterwards.
#include "WebApi.h"
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <errno.h>
#include "Ble.h"
#include "Settings.h"
#include "web.h"

namespace {
WebServer server(80);
constexpr size_t JSON_LIMIT = 6144;
constexpr const char *CONFIRM = "BESTAETIGEN";
LogBatch logBatch;  // owned by the HTTP task; keeps the large snapshot off its stack

struct UploadState { size_t bytes = 0; bool allowed = false, ok = false; } upload;

struct StatusSnapshot {
  bool connected, ready, armed, pending, held, updating;
  uint8_t level, pin, mode;
  uint32_t sent, skipped, pulses, filtered, session, nextMs;
  uint16_t remaining;
  TaskMetrics metrics;
  bool readyConfigured, readyEnabled, readyPulsing;
  uint32_t readySent, readySkipped;
};

void sendJson(JsonDocument &d, int code = 200) {
  String body;
  body.reserve(measureJson(d) + 1);
  serializeJson(d, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void reply(int code, const String &message) {
  JsonDocument d;
  d[code < 400 ? "message" : "error"] = message;
  sendJson(d, code);
}

bool authorized() {
  if (!server.authenticate("admin", adminPassword.c_str())) {
    server.requestAuthentication();
    return false;
  }
  if (server.method() == HTTP_POST && server.header("X-Shutter") != "1") {
    reply(403, "Missing request header.");
    return false;
  }
  return true;
}

bool readBody(JsonDocument &d) {
  const String &body = server.arg("plain");
  if (body.length() > JSON_LIMIT) return reply(413, "Settings are too large."), false;
  if (deserializeJson(d, body) || !d.is<JsonObject>()) return reply(400, "Expected a JSON object."), false;
  return true;
}

bool parseUint(const String &value, size_t maxLength, uint64_t limit, uint64_t &out) {
  if (!value.length() || value.length() > maxLength || !isdigit((unsigned char)value[0])) return false;
  char *end = nullptr;
  errno = 0;
  const unsigned long long parsed = strtoull(value.c_str(), &end, 10);
  if (errno || *end || parsed > limit) return false;
  out = parsed;
  return true;
}

// ---- Pages -------------------------------------------------------------------------------------

void handleIndex() {
  if (!authorized()) return;
  static const char *etag = "\"" FIRMWARE_VERSION "\"";
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("ETag", etag);
  if (server.header("If-None-Match") == etag) return server.send(304);
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char *)PAGE, sizeof(PAGE));
}

void handleStatus() {
  if (!authorized()) return;
  StatusSnapshot v;
  {
    Lock lock;
    const uint32_t now = millis();
    v.connected = ble::connected();
    v.ready = ble::ready();
    v.armed = engine.armed;
    v.pending = engine.pending;
    v.held = engine.held;
    v.level = digitalRead(engine.cfg.pin);
    v.pin = engine.cfg.pin;
    v.mode = engine.cfg.mode;
    v.sent = engine.sent;
    v.skipped = engine.skipped;
    v.pulses = engine.pulses;
    v.filtered = engine.filtered;
    v.session = engine.session;
    v.remaining = engine.remaining;
    v.nextMs = engine.pending ? max<int32_t>(0, int32_t(engine.next - now))
               : (engine.armed && engine.cfg.mode == 1 ? max<int32_t>(0, int32_t(engine.nextInterval - now)) : 0);
    v.updating = updating;
    v.metrics = taskMetrics;
    v.readyConfigured = readyButton.cfg.configured;
    v.readyEnabled = readyButton.enabled;
    v.readyPulsing = readyButton.pulsing;
    v.readySent = readyButton.sent;
    v.readySkipped = readyButton.skipped;
  }
  JsonDocument d;
  d["version"] = FIRMWARE_VERSION;
  d["connected"] = v.connected;
  d["ready"] = v.ready;
  d["armed"] = v.armed;
  d["pending"] = v.pending;
  d["held"] = v.held;
  d["level"] = v.level;
  d["pin"] = v.pin;
  d["mode"] = v.mode;
  d["sent"] = v.sent;
  d["skipped"] = v.skipped;
  d["pulses"] = v.pulses;
  d["filtered"] = v.filtered;
  d["session"] = v.session;
  d["remaining"] = v.remaining;
  d["nextMs"] = v.nextMs;
  d["updating"] = v.updating;
  d["readyButton"]["configured"] = v.readyConfigured;
  d["readyButton"]["enabled"] = v.readyEnabled;
  d["readyButton"]["pulsing"] = v.readyPulsing;
  d["readyButton"]["sent"] = v.readySent;
  d["readyButton"]["skipped"] = v.readySkipped;
  d["taskLastUs"] = v.metrics.lastUs;
  d["taskMaxUs"] = v.metrics.maxUs;
  d["taskMaxGapMs"] = v.metrics.maxGapMs;
  d["taskCycles"] = v.metrics.cycles;
  d["uptime"] = millis() / 1000;
  d["freeHeap"] = ESP.getFreeHeap();
  d["minHeap"] = ESP.getMinFreeHeap();
  d["flashSize"] = deviceInfo.flashSize;
  d["appSize"] = deviceInfo.appSize;
  d["otaSpace"] = deviceInfo.otaSpace;
  d["resetReason"] = deviceInfo.resetReason;
  const bool wifi = WiFi.status() == WL_CONNECTED;
  d["wifiConnected"] = wifi;
  d["ip"] = wifi ? WiFi.localIP().toString() : "";
  d["rssi"] = wifi ? WiFi.RSSI() : 0;
  d["apIP"] = WiFi.softAPIP().toString();
  d["clients"] = WiFi.softAPgetStationNum();
  d["rebootRequired"] = rebootRequired;
  d["bonds"] = ble::bondCount();
  sendJson(d);
}

// ---- Capture -----------------------------------------------------------------------------------

void handleGetConfig() {
  if (!authorized()) return;
  CaptureConfig c;
  {
    Lock lock;
    c = engine.cfg;
  }
  JsonDocument d;
  configToJson(d, c);
  sendJson(d);
}

void handlePostConfig() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  CaptureConfig c;
  String error;
  if (!parseCaptureConfig(d.as<JsonVariantConst>(), c, error)) return reply(400, error);
  bool conflict;
  {
    Lock lock;
    conflict = readyUses(c.pin);
  }
  if (conflict) return reply(409, "Capture input conflicts with the Ready button pins.");
  String saved;
  serializeJson(d, saved);
  // Persist first: never block the control task with a flash write under the lock.
  if (!prefs.putString("config2", saved)) return reply(500, "Save failed.");
  {
    Lock lock;
    stopCapture();
    pinMode(engine.cfg.pin, INPUT);
    pinMode(c.pin, c.pullup ? INPUT_PULLUP : INPUT);
    engine.configure(c, digitalRead(c.pin), millis());
    logEvent("Settings saved. Capture stopped.");
  }
  reply(200, "Saved. Capture stopped.");
}

void handleAction() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  const String action = d["action"] | "";
  int code = 200;
  String message = "Done.";
  {
    Lock lock;
    if (updating) code = 409, message = "Update in progress.";
    else if (action == "start") {
      if (!ble::ready()) code = 409, message = "Pair your phone and wait for Bluetooth ready.";
      else {
        stopCapture();
        engine.start(digitalRead(engine.cfg.pin), millis());
        logEvent("Capture started.");
      }
    } else if (action == "stop") {
      stopCapture();
      logEvent("Capture stopped.");
    } else if (action == "stopAll") {
      stopCapture();
      stopReady();
      logEvent("All outputs stopped.");
    } else if (action == "test") {
      if (!engine.request(millis(), ble::ready(), true)) code = 409, message = "Not ready, shot pending, or minimum spacing active.";
    } else if (action == "resetCounters") {
      if (engine.armed || engine.pending || engine.held) code = 409, message = "Stop capture first.";
      else {
        engine.resetCounters();
        logEvent("Counters cleared.");
      }
    } else if (action == "clearLog") eventLog.clear();
    else code = 400, message = "Unknown action.";
  }
  reply(code, message);
}

void handleLogs() {
  if (!authorized()) return;
  uint64_t after = 0, generation = 0;
  if (server.hasArg("after") && !parseUint(server.arg("after"), 20, UINT64_MAX, after)) return reply(400, "Invalid log cursor.");
  if (server.hasArg("generation") && !parseUint(server.arg("generation"), 10, UINT32_MAX, generation))
    return reply(400, "Invalid log generation.");
  {
    Lock lock;
    eventLog.snapshot(logBatch, after, uint32_t(generation));
  }
  JsonDocument d;
  d["generation"] = logBatch.generation;
  d["reset"] = logBatch.reset;
  char number[24];
  snprintf(number, sizeof(number), "%llu", (unsigned long long)logBatch.cursor);
  d["cursor"] = number;  // 64-bit ids travel as strings
  auto list = d["events"].to<JsonArray>();
  for (size_t i = 0; i < logBatch.count; i++) {
    const auto &item = logBatch.entries[i];
    auto e = list.add<JsonObject>();
    snprintf(number, sizeof(number), "%llu", (unsigned long long)item.id);
    e["id"] = number;
    e["ms"] = item.ms;
    e["text"] = item.text;
  }
  sendJson(d);
}

// ---- Network and system ------------------------------------------------------------------------

void handleGetNetwork() {
  if (!authorized()) return;
  JsonDocument d;
  d["ap"] = net.ap;
  d["ssid"] = net.ssid;
  d["hostname"] = net.hostname;
  d["bleName"] = net.bleName;
  d["channel"] = net.channel;
  d["hasWifiPassword"] = net.wifiPassword.length() > 0;
  sendJson(d);
}

void handlePostNetwork() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  for (const char *k : {"ap", "ssid", "hostname", "bleName"})
    if (!d[k].is<String>()) return reply(400, "Network names are missing.");
  const String ap = d["ap"], ssid = d["ssid"], host = d["hostname"], ble = d["bleName"];
  const int channel = d["channel"] | 0;
  if (ap.length() < 1 || ap.length() > 32 || ssid.length() > 32 || host.length() < 1 || host.length() > 24 ||
      ble.length() < 1 || ble.length() > 24 || !d["channel"].is<int>() || channel < 1 || channel > 11)
    return reply(400, "Name or channel out of range.");
  for (char ch : host)
    if (!isalnum((unsigned char)ch) && ch != '-') return reply(400, "Hostname: letters, numbers and hyphens only.");
  if (host[0] == '-' || host[host.length() - 1] == '-') return reply(400, "Hostname cannot start or end with a hyphen.");
  const String pass = d["password"] | "", admin = d["adminPassword"] | "", wifi = d["wifiPassword"] | "";
  const auto badSecret = [](const String &s) { return s.length() && (s.length() < 8 || s.length() > 63); };
  if (badSecret(pass) || badSecret(admin) || badSecret(wifi)) return reply(400, "Passwords: 8–63 characters. Empty keeps current.");
  {
    Lock lock;
    stopCapture();
  }
  bool ok = true;
  const auto save = [&](const char *key, const String &value) {
    if (prefs.getString(key, "\x01") == value) return;
    prefs.putString(key, value);
    ok = ok && prefs.getString(key, "\x01") == value;
  };
  save("ap", ap);
  save("ssid", ssid);
  save("host", host);
  save("ble", ble);
  if (net.channel != channel) ok = prefs.putUChar("channel", channel) > 0 && ok;
  if (pass.length()) save("password", pass);
  if (admin.length()) save("admin", admin);
  if (d["clearWifiPassword"] == true) save("wifipass", "");
  else if (wifi.length()) save("wifipass", wifi);
  if (!ok) return reply(500, "Network save failed. Check settings.");
  rebootRequired = true;  // keep current credentials until reboot so the session stays usable
  reply(200, "Saved. Restart to apply.");
}

void handleSystem() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  const String action = d["action"] | "";
  if (d["confirm"] != CONFIRM) return reply(400, "Confirmation required.");
  if (action != "reboot" && action != "forgetBonds" && action != "resetSettings") return reply(400, "Unknown system action.");
  {
    Lock lock;
    stopCapture();
    stopReady();
    logEventf("System action: %s", action.c_str());
  }
  if (action == "forgetBonds") ble::forgetBonds();
  if (action == "resetSettings") {
    prefs.remove("config2");
    for (const char *key : {"high", "down", "debounce", "cooldown", "hold"}) prefs.remove(key);
  }
  reply(200, "Restarting. Capture stays off.");
  scheduleReboot();
}

// ---- OTA ---------------------------------------------------------------------------------------

void setUpdating(bool value, const char *message = nullptr) {
  Lock lock;
  updating = value;
  if (message) logEvent(message);
}

void abortUpload() {
  Update.abort();
  upload.allowed = false;
  setUpdating(false);
}

// An ESP32 application image starts with 0xE9 and has esp_app_desc_t magic at byte 32.
bool isAppImage(const uint8_t *b, size_t n) {
  return n >= 36 && b[0] == 0xe9 && b[12] == 0 && b[13] == 0 && b[32] == 0x32 && b[33] == 0x54 && b[34] == 0xcd &&
         b[35] == 0xab;
}

void handleUpdateDone() {
  if (!authorized()) return;
  if (!upload.allowed) return reply(400, "No valid firmware upload.");
  upload.allowed = false;
  if (!upload.ok) return reply(400, "Update failed. Use the correct firmware.bin.");
  reply(200, "Updated. Restarting.");
  scheduleReboot();
}

void handleUploadChunk() {
  HTTPUpload &u = server.upload();
  switch (u.status) {
    case UPLOAD_FILE_START:
      upload = {};
      upload.allowed = server.authenticate("admin", adminPassword.c_str()) && server.header("X-Shutter") == "1";
      if (!upload.allowed) return;
      {
        Lock lock;
        stopCapture();
        stopReady();
        updating = true;
        logEvent("Firmware upload started.");
      }
      if (!u.filename.endsWith(".bin") || !Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        upload.allowed = false;
        setUpdating(false);
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (!upload.allowed) return;
      if (upload.bytes == 0 && !isAppImage(u.buf, u.currentSize)) return abortUpload();
      if (Update.write(u.buf, u.currentSize) != u.currentSize) return abortUpload();
      upload.bytes += u.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (!upload.allowed) return;
      upload.ok = Update.end(true);
      setUpdating(false, upload.ok ? "Update complete." : "Update failed.");
      break;
    case UPLOAD_FILE_ABORTED:
      abortUpload();
      break;
    default:
      break;
  }
}

// ---- Ready button ------------------------------------------------------------------------------

void handleGetReady() {
  if (!authorized()) return;
  ReadyConfig c;
  {
    Lock lock;
    c = readyButton.cfg;
  }
  JsonDocument d;
  d["configured"] = c.configured;
  d["input"] = c.input;
  d["output"] = c.output;
  d["debounce"] = c.debounce;
  d["pulse"] = c.pulse;
  d["cooldown"] = c.cooldown;
  sendJson(d);
}

void handlePostReady() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  ReadyConfig c;
  String error;
  if (!parseReadyConfig(d.as<JsonVariantConst>(), c, error)) return reply(400, error);
  bool conflict;
  {
    Lock lock;
    conflict = c.input == engine.cfg.pin || c.output == engine.cfg.pin;
  }
  if (conflict) return reply(409, "Button pins conflict with the capture input.");
  String stored;
  serializeJson(d, stored);
  if (prefs.getString("ready2", "") != stored && !prefs.putString("ready2", stored)) return reply(500, "Button save failed.");
  {
    Lock lock;
    stopReady();
    if (readyButton.cfg.configured) pinMode(readyButton.cfg.input, INPUT);
    readyButton.cfg = c;
    configureReadyPins();
    logEvent("Ready button saved. Disabled.");
  }
  reply(200, "Saved. Ready button disabled.");
}

void handleReadyAction() {
  if (!authorized()) return;
  JsonDocument d;
  if (!readBody(d)) return;
  const String action = d["action"] | "";
  int code = 200;
  String message = "Done.";
  {
    Lock lock;
    if (action == "disable") {
      stopReady();
      logEvent("Ready button disabled.");
    } else if (action == "enable") {
      if (updating || !readyButton.cfg.configured) code = 409, message = "Save button settings first.";
      else if (d["confirm"] != CONFIRM) code = 400, message = "Confirmation required.";
      else {
        readyButton.enable(digitalRead(readyButton.cfg.input), millis());
        logEvent("Ready button enabled.");
      }
    } else code = 400, message = "Unknown button action.";
  }
  reply(code, message);
}
}  // namespace

namespace webapi {
void begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/network", HTTP_GET, handleGetNetwork);
  server.on("/api/network", HTTP_POST, handlePostNetwork);
  server.on("/api/system", HTTP_POST, handleSystem);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUploadChunk);
  server.on("/api/ready/config", HTTP_GET, handleGetReady);
  server.on("/api/ready/config", HTTP_POST, handlePostReady);
  server.on("/api/ready/action", HTTP_POST, handleReadyAction);
  server.onNotFound([] { reply(404, "Not found."); });
  const char *headers[] = {"X-Shutter", "If-None-Match"};
  server.collectHeaders(headers, 2);
  server.begin();
}

void handle() { server.handleClient(); }
}  // namespace webapi
