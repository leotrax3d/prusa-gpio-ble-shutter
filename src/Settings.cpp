#include "Settings.h"

void configToJson(JsonDocument &d, const CaptureConfig &c) {
  d["schema"] = 2;
  d["mode"] = c.mode;
  d["key"] = c.key;
  d["pin"] = c.pin;
  d["activeHigh"] = c.activeHigh;
  d["pullup"] = c.pullup;
  d["disarmOnDisconnect"] = c.disarmOnDisconnect;
  d["debounce"] = c.debounce;
  d["cooldown"] = c.cooldown;
  d["hold"] = c.hold;
  d["delay"] = c.delay;
  d["every"] = c.every;
  d["burst"] = c.burst;
  d["burstGap"] = c.burstGap;
  d["interval"] = c.interval;
  d["startDelay"] = c.startDelay;
  d["maxFrames"] = c.maxFrames;
  d["autoStop"] = c.autoStop;
}

bool parseCaptureConfig(JsonVariantConst d, CaptureConfig &c, String &error) {
  if (!d.is<JsonObjectConst>()) return error = "Expected a settings object.", false;
  if (!d["schema"].is<int>() || d["schema"].as<int>() != 2) return error = "Settings require schema 2.", false;

  struct Range { const char *name; uint32_t low, high; };
  static constexpr Range ranges[] = {
      {"mode", 0, 2},         {"key", 0, 2},          {"pin", 0, 39},           {"debounce", 5, 500},
      {"cooldown", 100, 60000}, {"hold", 30, 1000},   {"delay", 0, 600000},     {"every", 1, 1000},
      {"burst", 1, 50},       {"burstGap", 100, 60000}, {"interval", 200, 86400000}, {"startDelay", 0, 600000},
      {"maxFrames", 0, 1000000}, {"autoStop", 0, 86400000}};
  for (const auto &r : ranges) {
    const auto v = d[r.name];
    if (!v.is<uint32_t>() || v.as<uint32_t>() < r.low || v.as<uint32_t>() > r.high)
      return error = String("Invalid value: ") + r.name, false;
  }
  for (const char *key : {"activeHigh", "pullup", "disarmOnDisconnect"})
    if (!d[key].is<bool>()) return error = String("Expected true or false: ") + key, false;

  c.mode = d["mode"];
  c.key = d["key"];
  c.pin = d["pin"];
  if (!pinAllowed(c.pin)) return error = "Allowed GPIOs: 13, 14, 25, 26, 27, 32, 33.", false;
  c.activeHigh = d["activeHigh"];
  c.pullup = d["pullup"];
  c.disarmOnDisconnect = d["disarmOnDisconnect"];
  c.debounce = d["debounce"];
  c.cooldown = d["cooldown"];
  c.hold = d["hold"];
  c.delay = d["delay"];
  c.every = d["every"];
  c.burst = d["burst"];
  c.burstGap = d["burstGap"];
  c.interval = d["interval"];
  c.startDelay = d["startDelay"];
  c.maxFrames = d["maxFrames"];
  c.autoStop = d["autoStop"];

  if (c.cooldown < c.hold + 30) return error = "Minimum spacing must be key hold + 30 ms or more.", false;
  if (c.burstGap < c.cooldown) return error = "Burst spacing must meet the minimum spacing.", false;
  if (c.mode == 1 && c.interval < c.delay + (c.burst - 1) * c.burstGap + c.cooldown)
    return error = "Interval is too short for this burst.", false;
  return true;
}

bool parseReadyConfig(JsonVariantConst d, ReadyConfig &c, String &error) {
  for (const char *k : {"input", "output", "debounce", "pulse", "cooldown"})
    if (!d[k].is<uint32_t>()) return error = "Invalid button settings.", false;
  const uint32_t in = d["input"], out = d["output"], debounce = d["debounce"], pulse = d["pulse"],
                 cooldown = d["cooldown"];
  if (!pinAllowed(in) || !pinAllowed(out) || in == out || debounce < 10 || debounce > 200 || pulse < 700 ||
      pulse > 2000 || cooldown < pulse + 500 || cooldown > 10000)
    return error = "Check pins and timing. Pulse: 700–2000 ms. Spacing: pulse + 500 ms or more.", false;
  c.configured = true;
  c.input = in;
  c.output = out;
  c.debounce = debounce;
  c.pulse = pulse;
  c.cooldown = cooldown;
  return true;
}

static String randomPassword() {
  char v[17];
  snprintf(v, sizeof(v), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  return v;
}

static void loadNetwork() {
  net.password = prefs.getString("password", "");
  if (net.password.length() < 8) {
    net.password = randomPassword();
    prefs.putString("password", net.password);
  }
  adminPassword = prefs.getString("admin", net.password);
  net.ap = prefs.getString("ap", "Prusa-Shutter");
  net.ssid = prefs.getString("ssid", "");
  net.wifiPassword = prefs.getString("wifipass", "");
  net.hostname = prefs.getString("host", "prusa-shutter");
  net.bleName = prefs.getString("ble", "Prusa Shutter");
  net.channel = prefs.getUChar("channel", 1);
}

static void loadCapture() {
  const String saved = prefs.getString("config2", "");
  if (!saved.length()) {
    // Migrate compatible timing settings from firmware 1.x.
    auto &c = engine.cfg;
    c.activeHigh = prefs.getBool("high", true);
    c.key = prefs.getBool("down", false) ? 1 : 0;
    c.debounce = constrain(prefs.getUInt("debounce", 30), 5u, 200u);
    c.cooldown = constrain(prefs.getUInt("cooldown", 1000), 300u, 10000u);
    c.hold = constrain(prefs.getUInt("hold", 80), 30u, 300u);
    c.burstGap = max<uint32_t>(1200, c.cooldown);
    return;
  }
  JsonDocument d;
  CaptureConfig c;
  String error;
  if (!deserializeJson(d, saved) && parseCaptureConfig(d.as<JsonVariantConst>(), c, error)) engine.cfg = c;
}

static void loadReady() {
  JsonDocument d;
  if (deserializeJson(d, prefs.getString("ready2", ""))) return;
  ReadyConfig c;
  String error;
  if (!parseReadyConfig(d.as<JsonVariantConst>(), c, error)) return;
  if (c.input != engine.cfg.pin && c.output != engine.cfg.pin) readyButton.cfg = c;
}

void loadSettings() {
  prefs.begin("shutter", false);
  loadNetwork();
  loadCapture();
  loadReady();
  configureReadyPins();
}
