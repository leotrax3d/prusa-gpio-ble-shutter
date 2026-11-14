#include "Ble.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <atomic>

namespace ble {
namespace {
NimBLEServer *server = nullptr;
NimBLECharacteristic *report = nullptr;
std::atomic<bool> isConnected{false}, isEncrypted{false}, subscribed{false}, dropped{false}, advertise{true};

// Consumer control: report 1, two bits (Volume Up 0xE9, Volume Down 0xEA) + padding.
uint8_t reportMap[] = {
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01, 0x15, 0x00, 0x25, 0x01, 0x09, 0xe9, 0x09,
    0xea, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0xc0};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
    isConnected = true;
    isEncrypted = false;
    subscribed = false;
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    isConnected = false;
    isEncrypted = false;
    subscribed = false;
    dropped = true;
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override { isEncrypted = info.isEncrypted(); }
};

class ReportCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t value) override { subscribed = (value & 1) != 0; }
};
}  // namespace

void begin(const char *name) {
  NimBLEDevice::init(name);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, Just Works, LE Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(false);  // the control task decides when to advertise again

  auto *hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Prusa Shutter Community");
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap(reportMap, sizeof(reportMap));
  report = hid->getInputReport(1);
  report->setCallbacks(new ReportCallbacks());
  server->start();

  auto *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x0180);  // generic HID
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName(name);
  adv->enableScanResponse(true);
  adv->start();
}

bool connected() { return isConnected.load(); }
bool ready() { return isConnected.load() && isEncrypted.load() && subscribed.load() && report; }
bool takeDisconnect() { return dropped.exchange(false); }

void press(uint8_t key) {
  if (!report) return;
  report->setValue(&key, 1);
  report->notify();
}

void release() {
  if (!report) return;
  uint8_t zero = 0;
  report->setValue(&zero, 1);
  if (isConnected.load()) report->notify();
}

void restartAdvertising() {
  if (advertise.load()) NimBLEDevice::startAdvertising();
}

int bondCount() { return NimBLEDevice::getNumBonds(); }

void forgetBonds() {
  advertise = false;
  NimBLEDevice::stopAdvertising();
  if (server)
    for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
  NimBLEDevice::deleteAllBonds();
}
}  // namespace ble
