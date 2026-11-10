#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_system.h>
#include <atomic>
#include "Trigger.h"
#include "web.h"

WebServer web(80);
Preferences prefs;
BLECharacteristic *report;
std::atomic<bool> connected{false};
bool armed = false, activeHigh = true, volumeDown = false, held = false, sentBefore = false;
uint32_t debounceMs = 30, cooldownMs = 1000, holdMs = 80;
uint32_t pressAt = 0, lastSent = 0, sent = 0, skipped = 0;
String password;
Trigger trigger(true, 0);
// Report 1: two consumer-control bits, Volume Increment and Decrement.
static uint8_t reportMap[] = {
  0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x01,
  0x15,0x00,0x25,0x01,0x09,0xe9,0x09,0xea,
  0x75,0x01,0x95,0x02,0x81,0x02,
  0x75,0x06,0x95,0x01,0x81,0x03,0xc0
};
class Callbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected.store(true); }
  void onDisconnect(BLEServer*) override {
    connected.store(false);
    BLEDevice::startAdvertising();
  }
};
bool auth() {
  if (web.authenticate("admin", password.c_str())) {
    if(web.method()==HTTP_POST && web.header("X-Shutter")!="1") { web.send(403,"text/plain","Missing request header"); return false; }
    return true;
  }
  web.requestAuthentication(); return false;
}
void json(int code, const String &body) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(code, "application/json", body);
}
bool fire() {
  uint32_t now = millis();
  if (!connected.load() || held || (sentBefore && uint32_t(now-lastSent)<cooldownMs)) {
    skipped++; return false;
  }
  uint8_t key = volumeDown ? 2 : 1;
  report->setValue(&key, 1); report->notify();
  held = true; pressAt = lastSent = now; sentBefore = true; sent++;
  return true;
}
bool number(const char *name, uint32_t low, uint32_t high, uint32_t &out) {
  String s=web.arg(name);
  if (!s.length() || s.length()>6) return false;
  for (unsigned i=0;i<s.length();i++) if(s[i]<'0'||s[i]>'9') return false;
  uint32_t v=s.toInt(); if(v<low||v>high) return false; out=v; return true;
}
void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  trigger.reset(digitalRead(TRIGGER_PIN), millis());
  prefs.begin("shutter", false);
  password = prefs.getString("password", "");
  if (password.length()!=16) {
    char value[17]; snprintf(value,sizeof(value),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    password=value; prefs.putString("password",password);
  }
  activeHigh=prefs.getBool("high",true); volumeDown=prefs.getBool("down",false);
  debounceMs=constrain(prefs.getUInt("debounce",30),5u,200u);
  cooldownMs=constrain(prefs.getUInt("cooldown",1000),300u,10000u);
  holdMs=constrain(prefs.getUInt("hold",80),30u,300u);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Prusa-Shutter",password.c_str(),1,false,2);
  Serial.printf("\nWLAN: Prusa-Shutter\nPasswort: %s\nWeb: http://192.168.4.1 (admin / gleiches Passwort)\n",password.c_str());
  BLEDevice::init("Prusa Shutter");
  auto server=BLEDevice::createServer(); server->setCallbacks(new Callbacks());
  auto hid=new BLEHIDDevice(server);
  report=hid->inputReport(1);
  report->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  hid->manufacturer()->setValue("Community project");
  hid->pnp(0x02,0x303a,0x0001,0x0100); hid->hidInfo(0,1);
  hid->reportMap(reportMap,sizeof(reportMap));
  auto security=new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);
  security->setCapability(ESP_IO_CAP_NONE);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  hid->startServices(); hid->setBatteryLevel(100);
  auto adv=BLEDevice::getAdvertising();
  adv->setAppearance(0x0180); adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true); adv->start();
  web.on("/",HTTP_GET,[]{ if(auth()) web.send_P(200,"text/html; charset=utf-8",PAGE); });
  web.on("/api/status",HTTP_GET,[]{
    if(!auth()) return;
    json(200,String("{\"connected\":")+(connected.load()?"true":"false")+
      ",\"armed\":"+(armed?"true":"false")+",\"level\":"+digitalRead(TRIGGER_PIN)+
      ",\"activeHigh\":"+(activeHigh?"true":"false")+",\"volumeDown\":"+(volumeDown?"true":"false")+
      ",\"debounce\":"+debounceMs+",\"cooldown\":"+cooldownMs+",\"hold\":"+holdMs+
      ",\"sent\":"+sent+",\"skipped\":"+skipped+"}");
  });
  web.on("/api/arm",HTTP_POST,[]{
    if(!auth()) return;
    if(web.arg("armed")!="0"&&web.arg("armed")!="1") {json(400,"{\"error\":\"Ungültiger Wert\"}");return;}
    armed=web.arg("armed")=="1";
    trigger.reset(digitalRead(TRIGGER_PIN),millis());
    json(200,"{\"ok\":true}");
  });
  web.on("/api/test",HTTP_POST,[]{if(auth()) json(fire()?200:409,"{\"message\":\"Status prüfen\"}");});
  web.on("/api/config",HTTP_POST,[]{
    if(!auth()) return;
    uint32_t d,c,h;
    if(!number("debounce",5,200,d)||!number("cooldown",300,10000,c)||!number("hold",30,300,h)||
       (web.arg("activeHigh")!="0"&&web.arg("activeHigh")!="1")||
       (web.arg("volumeDown")!="0"&&web.arg("volumeDown")!="1")) {json(400,"{\"error\":\"Werte außerhalb des Bereichs\"}");return;}
    armed=false; debounceMs=d; cooldownMs=c; holdMs=h;
    activeHigh=web.arg("activeHigh")=="1"; volumeDown=web.arg("volumeDown")=="1";
    prefs.putUInt("debounce",d); prefs.putUInt("cooldown",c); prefs.putUInt("hold",h);
    prefs.putBool("high",activeHigh); prefs.putBool("down",volumeDown);
    trigger.reset(digitalRead(TRIGGER_PIN),millis()); json(200,"{\"ok\":true}");
  });
  web.onNotFound([]{web.send(404,"text/plain","Nicht gefunden");});
  const char *headers[]={"X-Shutter"}; web.collectHeaders(headers,1);
  web.begin();
}
void loop() {
  // Release before serving HTTP so ordinary requests cannot prolong a keypress.
  if(held && (!connected.load() || uint32_t(millis()-pressAt)>=holdMs)) {
    uint8_t zero=0; report->setValue(&zero,1);
    if(connected.load()) report->notify();
    held=false;
  }
  web.handleClient();
  if(trigger.update(digitalRead(TRIGGER_PIN),millis(),activeHigh,debounceMs) && armed) fire();
  delay(1);
}
