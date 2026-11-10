# ESP32 changes · 2.0.2

## What changed

- **No firmware-image verification per status request.** Arduino-ESP32 2.0.17 implements `ESP.getSketchSize()` using `esp_image_verify()`. The application size, partition size, flash size and reset reason are now read once at startup.
- **Short read locks.** Status and configuration routes copy plain data under the capture mutex. JSON construction, serialization and HTTP responses happen afterwards.
- **Bounded logging.** A fixed 48-entry ring replaces dynamic `String` messages. The capture task formats messages directly into bounded character arrays. HTTP uses a separate fixed snapshot, avoiding a large array on the HTTP stack.
- **Less idle work.** The control task uses a 10 ms period when inactive and a 2 ms period for capture, an active Ready pulse, or servo work. `vTaskDelayUntil()` keeps the schedule steady; overruns do not cause a catch-up storm. The selected SDK uses a 1 kHz FreeRTOS tick.
- **No redundant settled-servo writes.** Hardware PWM keeps running without repeated `ledcWrite()` calls once the target is reached.
- **Cached HID descriptor.** The fast task reads the stored notification descriptor instead of looking it up on each cycle.
- **Fewer dashboard requests.** Default refresh is 3 s, with one completion-based polling loop. Hidden pages stop scheduling requests. Failed requests back off to at most one attempt per 30 s.
- **Incremental logs.** Clients request entries after their cursor. Generation changes handle reboot/clear; a client that falls behind receives a reset snapshot. The browser keeps existing rows and caps its log at 48 rows.
- **Conditional HTML reloads.** The gzip dashboard uses a firmware-version ETag. An authenticated unchanged reload can return HTTP 304.

## Checks

Measured with the browser against a simulated ESP32 API:

| Check | Result |
|---|---:|
| Periodic status requests during 10.2 s at defaults | 3 |
| Requests during a hidden-tab test window | 0 |
| Log entries transferred when unchanged | 0 |
| Entries transferred after one new event | 1 |
| Existing log DOM row retained after a delta | Yes |
| Log reset after clear/reboot | Passed |
| Offline backoff | Passed |

`test/log.cpp` checks zero C++ `new` allocations during append, ring wrap, truncation and snapshots. This does not measure allocator activity inside the BLE stack or the whole ESP32 runtime.

## Memory tradeoff

The fixed log and HTTP snapshot use more static RAM than an array of `String` handles. They make log storage bounded and remove per-message heap churn; this release does **not** claim lower total RAM use. See the build values below. It also includes the new Ready-button feature, so the binary-size comparison is not an isolated optimization benchmark.

| Build value | 2.0.1 | 2.0.2 (includes Ready) |
|---|---:|---:|
| Firmware image | 1,686,960 bytes | 1,700,784 bytes |
| Gzip dashboard | 12,224 bytes | 13,767 bytes |

Latest linker report: **70,108 bytes static RAM**, **1,694,205 bytes application flash**. OTA partition limit: 1,966,080 bytes.

## On-device measurements

Logs → Device now reports control-task work time and the largest gap between task starts. These are live values from the ESP32, not hard real-time guarantees. Screenshots use mock values.

No ESP32 is attached to the development environment. Actual CPU load, radio coexistence, notification latency, USB flashing, OTA and physical Ready-button operation still require hardware testing. Source-level work reduction and browser request counts do not establish a percentage speedup on the device.

Source: [Arduino-ESP32 2.0.17, Esp.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/Esp.cpp).
