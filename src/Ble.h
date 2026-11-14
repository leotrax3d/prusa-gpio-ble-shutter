#pragma once
#include <stdint.h>

// Bluetooth LE HID consumer-control device ("volume up/down" keys).
namespace ble {
void begin(const char *name);
// Connected, encrypted and the phone has subscribed to HID notifications.
bool ready();
bool connected();
// True once after each disconnect.
bool takeDisconnect();
void press(uint8_t key);  // 1 = volume up, 2 = volume down
void release();
void restartAdvertising();
int bondCount();
// Stops advertising, drops the link and deletes all stored bonds.
void forgetBonds();
}  // namespace ble
