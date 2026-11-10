# Ready button · ESP32

A normally open button on the ESP32 sends one low pulse to a free Hackerboard input. The printer executes `M1200` from its USB macro and sets Ready if its current state allows it. A queued Prusa Connect job can then start, depending on queue settings. This does not select or create a job.

## Wiring

Power both devices off before wiring. Defaults:

| Connection | Purpose |
|---|---|
| Button between GPIO32 and ESP32 GND | Button input, internal pull-up |
| ESP32 GPIO25 → Hackerboard IN4 | Open-drain Ready pulse |
| ESP32 GND → Hackerboard GND | Common ground; already present with the photo connection |

GPIO32 and GPIO25 are configurable. Capture, servo and button pins must be separate. The ESP32 supplies the signal pull-up on GPIO25 while powered. No extra resistor or solder bridge is required for that powered configuration, assuming reliable existing connectors.

**Keep the ESP32 powered while IN4 is configured as a macro input.** If the ESP32 is off, its internal pull-up is unavailable and IN4 can float. For defined behavior with the ESP32 off, fit a suitable external pull-up or use the Hackerboard's JP4 pull-up. Software cannot replace a resistor when the ESP32 has no power.

Do not put 5 V or 24 V on either signal pin.

## Setup

1. Save the Ready-page settings. This configures the output idle HIGH and disables the button.
2. Copy [`gcode/ready-button/macros/btn_4.gcode`](../gcode/ready-button/macros/btn_4.gcode) to **`macros/btn_4.gcode`** on the printer USB drive. The file contains only:

   ```gcode
   M1200
   ```

3. With the ESP32 powered and the output idle HIGH, configure Hackerboard pin 4 as input:

   ```gcode
   M262 P4 B1
   ```

4. Check the plate is clear, then enable the button in the dashboard. If it is already held down, release it before pressing again.
5. A press produces a low pulse, default 1000 ms, then releases the line HIGH. The duration exceeds the documented 500 ms GPIO input polling interval. Default minimum spacing is 2000 ms.

The output is open-drain; the button's two contacts only connect GPIO32 to ground. Do not connect the button directly to a power rail.

## Behavior

- Debounced press, one pulse per hold, cooldown, no queued pulse.
- Disabled after reboot, saving its settings, firmware update or Stop all.
- Capture/Bluetooth availability does not control the Ready button.
- The dashboard counts sent pulses, not confirmed printer-state changes.
- Disabling the button does not cancel a Ready state already accepted by the printer or stop a queued job that has started.
- Do not reuse IN4 for another function while this macro is installed.

The firmware reserves the configured button pins. To reassign them, move the Ready configuration to other unused pins before changing capture/servo assignments. When changing the output wiring, first ensure the printer input cannot float or trigger unexpectedly.

`M1200` is implemented as an internal Buddy command in the current Prusa source. Verify support and behavior on your installed printer firmware before queue operation. No physical printer/button test has been performed here.

Sources: [Buddy M1200](https://github.com/prusa3d/Prusa-Firmware-Buddy/blob/master/src/marlin_stubs/M1200.cpp), [GPIO module and USB macros](https://help.prusa3d.com/article/gpio-module_734695).
