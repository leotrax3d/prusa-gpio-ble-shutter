// Fast control task: capture engine and Ready button.
#include <driver/gpio.h>
#include "Ble.h"
#include "Shared.h"

void configureReadyPins() {
  const auto &c = readyButton.cfg;
  if (!c.configured) return;
  pinMode(c.input, INPUT_PULLUP);
  digitalWrite(c.output, HIGH);
  pinMode(c.output, OUTPUT_OPEN_DRAIN);
  gpio_pullup_en(gpio_num_t(c.output));
  digitalWrite(c.output, HIGH);
}

void stopReady() {
  readyButton.disable();
  if (readyButton.cfg.configured) digitalWrite(readyButton.cfg.output, HIGH);
}

bool readyUses(uint8_t pin) {
  const auto &c = readyButton.cfg;
  return c.configured && (pin == c.input || pin == c.output);
}

void stopCapture() {
  engine.stop();
  if (engine.held) {
    ble::release();
    engine.held = false;
  }
}

static uint8_t keyFor(const CaptureConfig &c, uint32_t sent) {
  if (c.key == 1) return 2;                     // volume down
  if (c.key == 2) return sent % 2 == 0 ? 2 : 1;  // alternating
  return 1;                                     // volume up
}

static uint32_t controlStep(uint32_t now) {
  if (ble::takeDisconnect()) {
    if (engine.cfg.disarmOnDisconnect) stopCapture();
    else {
      engine.pending = false;
      engine.remaining = 0;
    }
    logEvent("Bluetooth disconnected. Pending shots dropped.");
    ble::release();
    ble::restartAdvertising();
  }
  if (updating) return 10;

  if (readyButton.cfg.configured) {
    const int change = readyButton.tick(digitalRead(readyButton.cfg.input), now);
    if (change > 0) {
      digitalWrite(readyButton.cfg.output, LOW);
      logEvent("Ready pulse sent. Printer state not confirmed.");
    } else if (change < 0) {
      digitalWrite(readyButton.cfg.output, HIGH);
    }
  }

  const int action = engine.tick(digitalRead(engine.cfg.pin), ble::ready(), now);
  if (action < 0) ble::release();
  if (action > 0) {
    ble::press(keyFor(engine.cfg, engine.sent));
    logEventf("Key command #%lu sent.", (unsigned long)engine.sent);
  }

  const bool busy = engine.armed || engine.pending || engine.held || readyButton.pulsing;
  return busy ? 2 : 10;
}

static void controlTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t lastStart = millis();
  for (;;) {
    const uint32_t startedUs = micros(), now = millis();
    uint32_t periodMs;
    {
      Lock lock;
      periodMs = controlStep(now);
      taskMetrics.cycles++;
      taskMetrics.lastUs = micros() - startedUs;
      taskMetrics.maxUs = max(taskMetrics.maxUs, taskMetrics.lastUs);
      taskMetrics.maxGapMs = max(taskMetrics.maxGapMs, uint32_t(now - lastStart));
    }
    lastStart = now;
    const TickType_t period = max<TickType_t>(1, pdMS_TO_TICKS(periodMs));
    if (xTaskGetTickCount() - lastWake > period) lastWake = xTaskGetTickCount();  // no catch-up after a stall
    vTaskDelayUntil(&lastWake, period);
  }
}

void startControlTask() {
  if (xTaskCreatePinnedToCore(controlTask, "capture", 6144, nullptr, 2, nullptr, 1) != pdPASS) {
    Serial.println("Capture task failed");
    for (;;) delay(1000);
  }
}
