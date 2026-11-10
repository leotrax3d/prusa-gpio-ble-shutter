#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_gap_ble_api.h>
#include <esp_system.h>
#include <atomic>
#include "Engine.h"
#include "ServoProtocol.h"
#include "web.h"

WebServer web(80);
Preferences prefs;
CaptureEngine engine;
SemaphoreHandle_t guard;
BLECharacteristic *report=nullptr;
BLEServer *bleServer=nullptr;
std::atomic<bool> connected{false}, encrypted{false}, disconnected{false}, allowAdvertising{true};
struct Lock {Lock(){xSemaphoreTake(guard,portMAX_DELAY);}~Lock(){xSemaphoreGive(guard);}};
struct NetworkConfig {
 String ap="Prusa-Shutter", password, ssid="", wifiPassword="", hostname="prusa-shutter", bleName="Prusa Shutter";
 uint8_t channel=1;
} net;
struct ServoRule {String when="start";uint32_t layer=1;uint16_t angle=135;};
ServoRule rules[8];uint8_t ruleCount=0;
ServoProtocol servoProtocol;
struct ServoConfig {bool automation=false;uint8_t commandPin=33;uint8_t pin=26;uint16_t degrees=270,minUs=1000,maxUs=2000,stepUs=10;} servo;
bool servoEnabled=false;
uint16_t servoCurrent=0,servoTarget=0;
uint32_t servoLast=0;
constexpr uint8_t SERVO_CHANNEL=6;
void disableServo(){if(servoEnabled){ledcWrite(SERVO_CHANNEL,0);ledcDetachPin(servo.pin);pinMode(servo.pin,INPUT);}servoEnabled=false;servoCurrent=servoTarget=0;servoProtocol.reset(digitalRead(servo.commandPin),millis());}
void logEvent(const String &text);
void servoTick(uint32_t now){
 if(servo.automation){
  int slot=servoProtocol.tick(digitalRead(servo.commandPin),now);
  if(slot&&servoEnabled&&slot<=ruleCount){
   servoTarget=servo.minUs+(uint32_t)(servo.maxUs-servo.minUs)*rules[slot-1].angle/servo.degrees;
   logEvent("Servo-Aktion "+String(slot)+": "+String(rules[slot-1].angle)+" Grad.");
  }
 }
 if(!servoEnabled||!servoTarget||uint32_t(now-servoLast)<20)return;
 servoLast=now;
 if(!servoCurrent)servoCurrent=servoTarget;
 else if(servoCurrent<servoTarget)servoCurrent=min((uint16_t)(servoCurrent+servo.stepUs),servoTarget);
 else if(servoCurrent>servoTarget)servoCurrent=max((int)servoCurrent-servo.stepUs,(int)servoTarget);
 ledcWrite(SERVO_CHANNEL,(uint32_t)servoCurrent*65535/20000);
}
String adminPassword;
size_t uploadedBytes=0;
bool updating=false, uploadAllowed=false, uploadOK=false, rebootRequired=false;
uint32_t rebootAt=0;
const uint32_t JSON_LIMIT=6144;
struct Event{uint32_t ms;String text;};
Event events[48];uint32_t eventCount=0;
void logEvent(const String &text){events[eventCount%48]={millis(),text};eventCount++;}
void response(int code,const String &message){JsonDocument d;d[code<400?"message":"error"]=message;String s;serializeJson(d,s);web.sendHeader("Cache-Control","no-store");web.send(code,"application/json",s);}
void sendJson(JsonDocument &d){String s;serializeJson(d,s);web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",s);}
bool auth(){
 if(!web.authenticate("admin",adminPassword.c_str())){web.requestAuthentication();return false;}
 if(web.method()==HTTP_POST&&web.header("X-Shutter")!="1"){response(403,"Anfrageheader fehlt.");return false;}
 return true;
}
bool readJson(JsonDocument &d){
 if(web.arg("plain").length()>JSON_LIMIT){response(413,"Konfiguration zu groß.");return false;}
 if(deserializeJson(d,web.arg("plain"))||!d.is<JsonObject>()){response(400,"Gültiges JSON-Objekt erforderlich.");return false;}
 return true;
}
void configJson(JsonDocument &d,const CaptureConfig &c){
 d["schema"]=2;d["mode"]=c.mode;d["key"]=c.key;d["pin"]=c.pin;d["activeHigh"]=c.activeHigh;
 d["pullup"]=c.pullup;d["disarmOnDisconnect"]=c.disarmOnDisconnect;
 d["debounce"]=c.debounce;d["cooldown"]=c.cooldown;d["hold"]=c.hold;d["delay"]=c.delay;
 d["every"]=c.every;d["burst"]=c.burst;d["burstGap"]=c.burstGap;d["interval"]=c.interval;
 d["startDelay"]=c.startDelay;d["maxFrames"]=c.maxFrames;d["autoStop"]=c.autoStop;
}
bool parseConfig(JsonVariantConst d,CaptureConfig &c,String &error){
 if(!d.is<JsonObjectConst>()){error="Konfiguration muss ein Objekt sein.";return false;}
 if(d["schema"].isNull()||!d["schema"].is<int>()||d["schema"].as<int>()!=2){error="Konfiguration benötigt Schema 2.";return false;}
 struct Field{const char *name;uint32_t low,high;};
 const Field fields[]={{"mode",0,2},{"key",0,2},{"pin",0,39},{"debounce",5,500},{"cooldown",100,60000},{"hold",30,1000},{"delay",0,600000},{"every",1,1000},{"burst",1,50},{"burstGap",100,60000},{"interval",200,86400000},{"startDelay",0,600000},{"maxFrames",0,1000000},{"autoStop",0,86400000}};
 for(auto &f:fields){if(!d[f.name].is<uint32_t>()||d[f.name].as<uint32_t>()<f.low||d[f.name].as<uint32_t>()>f.high){error=String("Ungültiger Wert: ")+f.name;return false;}}
 for(auto key:{"activeHigh","pullup","disarmOnDisconnect"})if(!d[key].is<bool>()){error=String("Boolean erwartet: ")+key;return false;}
 c.mode=d["mode"];c.key=d["key"];c.pin=d["pin"];
 if(c.pin!=13&&c.pin!=14&&c.pin!=25&&c.pin!=26&&c.pin!=27&&c.pin!=32&&c.pin!=33){error="Erlaubte GPIOs: 13, 14, 25, 26, 27, 32, 33.";return false;}
 c.activeHigh=d["activeHigh"];c.pullup=d["pullup"];c.disarmOnDisconnect=d["disarmOnDisconnect"];
 c.debounce=d["debounce"];c.cooldown=d["cooldown"];c.hold=d["hold"];c.delay=d["delay"];
 c.every=d["every"];c.burst=d["burst"];c.burstGap=d["burstGap"];c.interval=d["interval"];
 c.startDelay=d["startDelay"];c.maxFrames=d["maxFrames"];c.autoStop=d["autoStop"];
 if(c.cooldown<c.hold+30){error="Mindestabstand muss Tastendauer + 30 ms erreichen.";return false;}
 if(c.burstGap<c.cooldown){error="Serienabstand muss mindestens dem Mindestabstand entsprechen.";return false;}
 if(c.mode==1&&c.interval<c.delay+(c.burst-1)*c.burstGap+c.cooldown){error="Intervall ist kürzer als Verzögerung + Serie + Mindestabstand.";return false;}
 return true;
}
bool hidReady(){
 if(!connected.load()||!encrypted.load()||!report)return false;
 auto cccd=static_cast<BLE2902*>(report->getDescriptorByUUID(BLEUUID((uint16_t)0x2902)));
 return cccd&&cccd->getNotifications();
}
void keyRelease(){uint8_t zero=0;report->setValue(&zero,1);if(connected.load())report->notify();}
void stopCapture(){engine.stop();if(engine.held){keyRelease();engine.held=false;}}
class ServerCallbacks:public BLEServerCallbacks {
 void onConnect(BLEServer*)override{connected=true;encrypted=false;}
 void onDisconnect(BLEServer*)override{connected=false;encrypted=false;disconnected=true;}
};
class SecurityCallbacks:public BLESecurityCallbacks {
 uint32_t onPassKeyRequest()override{return 0;}
 void onPassKeyNotify(uint32_t)override{}
 bool onConfirmPIN(uint32_t)override{return false;}
 bool onSecurityRequest()override{return true;}
 void onAuthenticationComplete(esp_ble_auth_cmpl_t c)override{encrypted=c.success;}
};
void captureTask(void*){
 for(;;){
  {
   Lock lock;
   if(disconnected.exchange(false)){if(engine.cfg.disarmOnDisconnect)stopCapture();else {engine.pending=false;engine.remaining=0;}logEvent("Bluetooth getrennt; ausstehende Fotos verworfen.");keyRelease();if(allowAdvertising.load())BLEDevice::startAdvertising();}
   if(!updating){
    servoTick(millis());
    int action=engine.tick(digitalRead(engine.cfg.pin),hidReady(),millis());
    if(action<0)keyRelease();
    if(action>0){uint8_t key=engine.cfg.key==1?2:(engine.cfg.key==2&&engine.sent%2==0?2:1);report->setValue(&key,1);report->notify();logEvent("Tastenbefehl #"+String(engine.sent)+" gesendet.");}
   }
  }
  vTaskDelay(pdMS_TO_TICKS(2));
 }
}
void setupBLE(){
 static uint8_t map[]={0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x01,0x15,0x00,0x25,0x01,0x09,0xe9,0x09,0xea,0x75,0x01,0x95,0x02,0x81,0x02,0x75,0x06,0x95,0x01,0x81,0x03,0xc0};
 BLEDevice::init(net.bleName.c_str());BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
 bleServer=BLEDevice::createServer();bleServer->setCallbacks(new ServerCallbacks());
 auto hid=new BLEHIDDevice(bleServer);report=hid->inputReport(1);
 report->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED|ESP_GATT_PERM_WRITE_ENCRYPTED);
 hid->manufacturer()->setValue("Prusa Shutter Community");hid->hidInfo(0,1);hid->reportMap(map,sizeof(map));
 auto security=new BLESecurity();security->setAuthenticationMode(ESP_LE_AUTH_BOND);security->setCapability(ESP_IO_CAP_NONE);
 security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK);
 hid->startServices();
 auto adv=BLEDevice::getAdvertising();adv->setAppearance(0x0180);adv->addServiceUUID(hid->hidService()->getUUID());adv->setScanResponse(true);adv->start();
}
String randomPassword(){char v[17];snprintf(v,sizeof(v),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());return String(v);}
void loadConfig(){
 prefs.begin("shutter",false);
 net.password=prefs.getString("password","");if(net.password.length()<8){net.password=randomPassword();prefs.putString("password",net.password);}
 adminPassword=prefs.getString("admin",net.password);
 net.ap=prefs.getString("ap","Prusa-Shutter");net.ssid=prefs.getString("ssid","");net.wifiPassword=prefs.getString("wifipass","");
 net.hostname=prefs.getString("host","prusa-shutter");net.bleName=prefs.getString("ble","Prusa Shutter");net.channel=prefs.getUChar("channel",1);
 JsonDocument d;String err;CaptureConfig c;
 String saved=prefs.getString("config2","");
 if(saved.length()&&!deserializeJson(d,saved)&&parseConfig(d.as<JsonVariantConst>(),c,err))engine.cfg=c;
 else if(!saved.length()){
  // Preserve the first release's compatible timing settings during migration.
  engine.cfg.activeHigh=prefs.getBool("high",true);engine.cfg.key=prefs.getBool("down",false)?1:0;
  engine.cfg.debounce=constrain(prefs.getUInt("debounce",30),5u,200u);
  engine.cfg.cooldown=constrain(prefs.getUInt("cooldown",1000),300u,10000u);
  engine.cfg.hold=constrain(prefs.getUInt("hold",80),30u,300u);
  engine.cfg.burstGap=max(1200u,engine.cfg.cooldown);
 }
 JsonDocument plan;String storedServo=prefs.getString("servo2","");
 if(storedServo.length()&&!deserializeJson(plan,storedServo)){
  int p=plan["pin"]|26,cmd=plan["commandPin"]|33,deg=plan["degrees"]|270;
  int lo=plan["minUs"]|1000,hi=plan["maxUs"]|2000,step=plan["stepUs"]|10;
  auto validPin=[](int n){return n==13||n==14||n==25||n==26||n==27||n==32||n==33;};
  if(validPin(p)&&validPin(cmd)&&p!=cmd&&(deg==180||deg==270)&&lo>=500&&hi<=2500&&hi>=lo+100&&step>=1&&step<=100){
   servo.pin=p;servo.commandPin=cmd;servo.degrees=deg;servo.minUs=lo;servo.maxUs=hi;servo.stepUs=step;
   servo.automation=(plan["automation"]|false)&&cmd!=engine.cfg.pin&&p!=engine.cfg.pin;
   for(JsonObject r:plan["rules"].as<JsonArray>()){
    if(ruleCount>=8)break;String when=r["when"]|"";int angle=r["angle"]|-1;
    if((when!="start"&&when!="layer"&&when!="layerNumber"&&when!="end")||angle<0||angle>servo.degrees)continue;
    rules[ruleCount].when=when;rules[ruleCount].layer=r["layer"]|1u;rules[ruleCount].angle=angle;ruleCount++;
   }
  }
 }
 if(servo.automation)pinMode(servo.commandPin,INPUT_PULLUP);servoProtocol.reset(digitalRead(servo.commandPin),millis());
}
void configureNetwork(){
 WiFi.mode(net.ssid.length()?WIFI_AP_STA:WIFI_AP);WiFi.setHostname(net.hostname.c_str());
 WiFi.softAP(net.ap.c_str(),net.password.c_str(),net.channel,false,4);
 if(net.ssid.length())WiFi.begin(net.ssid.c_str(),net.wifiPassword.c_str());
 if(MDNS.begin(net.hostname.c_str()))MDNS.addService("http","tcp",80);
 Serial.printf("\nPrusa Shutter %s\nWLAN: %s\nWLAN-Passwort: %s\nDashboard: http://192.168.4.1\nLogin: admin / %s\n",FIRMWARE_VERSION,net.ap.c_str(),net.password.c_str(),adminPassword.c_str());
}
void scheduleReboot(){rebootAt=millis()+1500;}
void setupRoutes(){
 web.on("/",HTTP_GET,[]{if(!auth())return;web.sendHeader("Content-Encoding","gzip");web.sendHeader("Cache-Control","no-cache");web.send_P(200,"text/html; charset=utf-8",(const char*)PAGE,sizeof(PAGE));});
 web.on("/api/status",HTTP_GET,[]{
  if(!auth())return;JsonDocument d;
  {Lock lock;
   d["version"]=FIRMWARE_VERSION;d["connected"]=connected.load();d["ready"]=hidReady();d["armed"]=engine.armed;
   d["pending"]=engine.pending;d["held"]=engine.held;d["level"]=digitalRead(engine.cfg.pin);d["pin"]=engine.cfg.pin;
   d["sent"]=engine.sent;d["skipped"]=engine.skipped;d["pulses"]=engine.pulses;d["filtered"]=engine.filtered;d["session"]=engine.session;
   d["remaining"]=engine.remaining;d["mode"]=engine.cfg.mode;
   d["nextMs"]=engine.pending?max(0,(int32_t)(engine.next-millis())):(engine.armed&&engine.cfg.mode==1?max(0,(int32_t)(engine.nextInterval-millis())):0);
   d["updating"]=updating;
   d["servo"]["enabled"]=servoEnabled;d["servo"]["targetUs"]=servoTarget;d["servo"]["currentUs"]=servoCurrent;
  }
  d["uptime"]=millis()/1000;d["freeHeap"]=ESP.getFreeHeap();d["minHeap"]=ESP.getMinFreeHeap();d["flashSize"]=ESP.getFlashChipSize();d["appSize"]=ESP.getSketchSize();d["otaSpace"]=ESP.getFreeSketchSpace();
  d["wifiConnected"]=WiFi.status()==WL_CONNECTED;d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"";d["rssi"]=WiFi.status()==WL_CONNECTED?WiFi.RSSI():0;
  d["apIP"]=WiFi.softAPIP().toString();d["clients"]=WiFi.softAPgetStationNum();d["rebootRequired"]=rebootRequired;
  d["resetReason"]=(int)esp_reset_reason();d["bonds"]=esp_ble_get_bond_device_num();sendJson(d);
 });
 web.on("/api/config",HTTP_GET,[]{if(!auth())return;JsonDocument d;{Lock lock;configJson(d,engine.cfg);}sendJson(d);});
 web.on("/api/config",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;CaptureConfig c;String error;
  if(!parseConfig(d.as<JsonVariantConst>(),c,error)){response(400,error);return;}
  bool pinConflict;{Lock lock;pinConflict=(c.pin==servo.pin&&servoEnabled)||(servo.automation&&c.pin==servo.commandPin);}
  if(pinConflict){response(409,"Foto-Eingang kollidiert mit Servo-Signal oder aktivem Befehlseingang.");return;}
  String saved;serializeJson(d,saved);
  // Persist first; never block the timing task with a flash write under the mutex.
  if(!prefs.putString("config2",saved)){response(500,"Speichern fehlgeschlagen.");return;}
  {Lock lock;stopCapture();pinMode(engine.cfg.pin,INPUT);pinMode(c.pin,c.pullup?INPUT_PULLUP:INPUT);engine.configure(c,digitalRead(c.pin),millis());logEvent("Einstellungen gespeichert; Aufnahme gestoppt.");}
  response(200,"Gespeichert. Aufnahme ist gestoppt.");
 });
 web.on("/api/action",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;String action=d["action"]|"";
  int code=200;String message="Aktion ausgeführt.";
  {
   Lock lock;
   if(updating){code=409;message="Update läuft.";}
   else if(action=="start"){
    if(!hidReady()){code=409;message="Smartphone koppeln und HID-Bereitschaft abwarten.";}
    else{stopCapture();engine.start(digitalRead(engine.cfg.pin),millis());logEvent("Aufnahme aktiviert.");}
   }else if(action=="stop"){stopCapture();logEvent("Aufnahme gestoppt.");}
   else if(action=="stopAll"){stopCapture();disableServo();logEvent("Aufnahme und Servo-Signal gestoppt.");}
   else if(action=="test"){
    if(!engine.request(millis(),hidReady(),true)){code=409;message="Nicht bereit, Aufnahme ausstehend oder Mindestabstand aktiv.";}
   }else if(action=="resetCounters"){
    if(engine.armed||engine.pending||engine.held){code=409;message="Aufnahme zuerst stoppen.";}
    else{engine.resetCounters();logEvent("Zähler zurückgesetzt.");}
   }else if(action=="clearLog")eventCount=0;
   else{code=400;message="Unbekannte Aktion.";}
  }
  response(code,message);
 });
 web.on("/api/logs",HTTP_GET,[]{if(!auth())return;JsonDocument d;auto list=d["events"].to<JsonArray>();{Lock lock;uint32_t first=eventCount>48?eventCount-48:0;for(uint32_t i=first;i<eventCount;i++){auto e=list.add<JsonObject>();e["ms"]=events[i%48].ms;e["text"]=events[i%48].text;}}sendJson(d);});
 web.on("/api/network",HTTP_GET,[]{if(!auth())return;JsonDocument d;d["ap"]=net.ap;d["ssid"]=net.ssid;d["hostname"]=net.hostname;d["bleName"]=net.bleName;d["channel"]=net.channel;d["hasWifiPassword"]=net.wifiPassword.length()>0;sendJson(d);});
 web.on("/api/network",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  for(auto k:{"ap","ssid","hostname","bleName"})if(!d[k].is<String>()){response(400,"Netzwerknamen fehlen.");return;}
  String ap=d["ap"],ssid=d["ssid"],host=d["hostname"],ble=d["bleName"];
  if(ap.length()<1||ap.length()>32||ssid.length()>32||host.length()<1||host.length()>24||ble.length()<1||ble.length()>24||!d["channel"].is<int>()||d["channel"].as<int>()<1||d["channel"].as<int>()>11){response(400,"Namen oder Kanal außerhalb des Bereichs.");return;}
  for(unsigned i=0;i<host.length();i++)if(!isalnum((unsigned char)host[i])&&host[i]!='-'){response(400,"Hostname: nur Buchstaben, Zahlen, Bindestrich.");return;}
  if(host[0]=='-'||host[host.length()-1]=='-'){response(400,"Hostname darf nicht mit Bindestrich beginnen/enden.");return;}
  String pass=d["password"]|"",admin=d["adminPassword"]|"",wifi=d["wifiPassword"]|"";
  if((pass.length()&&(pass.length()<8||pass.length()>63))||(admin.length()&&(admin.length()<8||admin.length()>63))||wifi.length()>63||(wifi.length()>0&&wifi.length()<8)){response(400,"Passwörter: 8–63 Zeichen, leer = unverändert.");return;}
  {Lock lock;stopCapture();}
  bool ok=true;
  auto save=[&](const char*k,const String&v){if(prefs.getString(k,"\x01")!=v){prefs.putString(k,v);ok=(prefs.getString(k,"\x01")==v)&&ok;}};
  save("ap",ap);save("ssid",ssid);save("host",host);save("ble",ble);
  uint8_t ch=d["channel"];if(net.channel!=ch)ok=prefs.putUChar("channel",ch)>0&&ok;
  if(pass.length())save("password",pass);if(admin.length())save("admin",admin);
  if(d["clearWifiPassword"]==true)save("wifipass","");else if(wifi.length())save("wifipass",wifi);
  if(!ok){response(500,"Netzwerk nicht vollständig gespeichert. Werte erneut prüfen.");return;}
  // Keep current credentials until reboot, so users can finish their session.
  rebootRequired=true;response(200,"Gespeichert. Neustart übernimmt Netzwerk und Passwörter.");
 });
 web.on("/api/system",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;String action=d["action"]|"";
  if(d["confirm"]!="BESTAETIGEN"){response(400,"Bestätigung fehlt.");return;}
  if(action!="reboot"&&action!="forgetBonds"&&action!="resetSettings"){response(400,"Unbekannte Systemaktion.");return;}
  {Lock lock;stopCapture();disableServo();logEvent("Systemaktion: "+action);}
  if(action=="forgetBonds"){
   // Stop reconnects while bonds are being deleted; reboot after API response.
   allowAdvertising=false;BLEDevice::getAdvertising()->stop();if(connected.load())bleServer->disconnect(bleServer->getConnId());
   int count=esp_ble_get_bond_device_num();
   if(count>0){auto bonds=(esp_ble_bond_dev_t*)calloc(count,sizeof(esp_ble_bond_dev_t));if(!bonds){response(500,"Nicht genug Speicher.");return;}esp_ble_get_bond_device_list(&count,bonds);for(int i=0;i<count;i++)esp_ble_remove_bond_device(bonds[i].bd_addr);free(bonds);}
  }
  if(action=="resetSettings"){prefs.remove("config2");for(auto key:{"high","down","debounce","cooldown","hold"})prefs.remove(key);}
  response(200,"Neustart in Kürze. Aufnahme bleibt deaktiviert.");scheduleReboot();
 });
 web.on("/api/update",HTTP_POST,[]{
  if(!auth())return;
  if(!uploadAllowed){response(400,"Keine autorisierte Firmware empfangen.");return;}
  uploadAllowed=false;
  if(uploadOK){response(200,"Update installiert. Neustart in Kürze.");scheduleReboot();}
  else {response(400,"Update fehlgeschlagen. Nur passende firmware.bin verwenden.");}
 },[]{
  auto &upload=web.upload();
  if(upload.status==UPLOAD_FILE_START){
   uploadAllowed=web.authenticate("admin",adminPassword.c_str())&&web.header("X-Shutter")=="1";
   uploadOK=false;uploadedBytes=0;if(!uploadAllowed)return;
   {Lock lock;stopCapture();disableServo();updating=true;logEvent("Firmware-Upload gestartet.");}
   if(!upload.filename.endsWith(".bin")||!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){uploadAllowed=false;Lock lock;updating=false;}
  }else if(upload.status==UPLOAD_FILE_WRITE&&uploadAllowed){
   // Application images contain esp_app_desc_t magic at byte 32. Bootloader/merged images do not.
   if(uploadedBytes==0&&(upload.currentSize<36||upload.buf[0]!=0xe9||upload.buf[12]!=0||upload.buf[13]!=0||upload.buf[32]!=0x32||upload.buf[33]!=0x54||upload.buf[34]!=0xcd||upload.buf[35]!=0xab)){
    Update.abort();uploadAllowed=false;Lock lock;updating=false;return;
   }
   if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize){Update.abort();uploadAllowed=false;Lock lock;updating=false;}
   uploadedBytes+=upload.currentSize;
  }else if(upload.status==UPLOAD_FILE_END&&uploadAllowed){
   uploadOK=Update.end(true);Lock lock;updating=false;logEvent(uploadOK?"Update erfolgreich.":"Update fehlgeschlagen.");
  }else if(upload.status==UPLOAD_FILE_ABORTED){Update.abort();uploadAllowed=false;Lock lock;updating=false;}
 });

 web.on("/api/servo/config",HTTP_GET,[]{if(!auth())return;JsonDocument d;{Lock lock;d["pin"]=servo.pin;d["degrees"]=servo.degrees;d["minUs"]=servo.minUs;d["maxUs"]=servo.maxUs;d["stepUs"]=servo.stepUs;d["automation"]=servo.automation;d["commandPin"]=servo.commandPin;auto list=d["rules"].to<JsonArray>();for(int i=0;i<ruleCount;i++){auto r=list.add<JsonObject>();r["when"]=rules[i].when;r["layer"]=rules[i].layer;r["angle"]=rules[i].angle;}}sendJson(d);});
 web.on("/api/servo/config",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  for(auto k:{"pin","degrees","minUs","maxUs","stepUs","commandPin"})if(!d[k].is<uint16_t>()){response(400,"Ungültige Servo-Werte.");return;}
  uint16_t p=d["pin"],deg=d["degrees"],lo=d["minUs"],hi=d["maxUs"],step=d["stepUs"],command=d["commandPin"];
  if((p!=13&&p!=14&&p!=25&&p!=26&&p!=27&&p!=32&&p!=33)||(deg!=180&&deg!=270)||lo<500||hi>2500||hi<lo+100||step<1||step>100){response(400,"Servo-Pin, Pulsgrenzen oder Schrittweite ungültig.");return;}
  if(!d["automation"].is<bool>()||!d["rules"].is<JsonArray>()||d["rules"].size()>8||(command!=13&&command!=14&&command!=25&&command!=26&&command!=27&&command!=32&&command!=33)||command==p){response(400,"Ungültiger Befehlseingang oder Aktionsplan (max. 8 Aktionen).");return;}
  for(JsonObject r:d["rules"].as<JsonArray>()){
   String when=r["when"]|"";
   if((when!="start"&&when!="layer"&&when!="layerNumber"&&when!="end")||!r["angle"].is<uint16_t>()||r["angle"].as<uint16_t>()>deg||!r["layer"].is<uint32_t>()||r["layer"].as<uint32_t>()>100000){response(400,"Ungültige Servo-Aktion.");return;}
  }
  bool conflict;{Lock lock;conflict=p==engine.cfg.pin||(d["automation"].as<bool>()&&command==engine.cfg.pin);if(!conflict)disableServo();}
  if(conflict){response(409,"Servo, Befehlseingang und Foto-Eingang benötigen verschiedene Pins.");return;}
  String stored;serializeJson(d,stored);
  if(!prefs.putString("servo2",stored)){response(500,"Servo-Konfiguration konnte nicht gespeichert werden.");return;}
  {Lock lock;if(servo.automation)pinMode(servo.commandPin,INPUT);servo.pin=p;servo.degrees=deg;servo.minUs=lo;servo.maxUs=hi;servo.stepUs=step;servo.automation=d["automation"];servo.commandPin=command;ruleCount=0;
   for(JsonObject r:d["rules"].as<JsonArray>()){rules[ruleCount].when=r["when"].as<String>();rules[ruleCount].layer=r["layer"];rules[ruleCount].angle=r["angle"];ruleCount++;}
   if(servo.automation)pinMode(command,INPUT_PULLUP);servoProtocol.reset(digitalRead(command),millis());logEvent("Servo und Aktionsplan gespeichert; Signal aus.");}
  response(200,"Kalibrierung gespeichert. Servo-Signal ausgeschaltet.");
 });
 web.on("/api/servo/action",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  String action=d["action"]|"";int code=200;String message="Servo-Befehl übernommen.";
  {
   Lock lock;
   if(updating){code=409;message="Update läuft.";}
   else if(action=="disable"){disableServo();logEvent("Servo-Signal aus.");}
   else if(d["confirm"]!="BESTAETIGEN"){code=400;message="Bestätigung fehlt.";}
   else if(action=="enable"){
    if(servo.pin==engine.cfg.pin){code=409;message="Servo-Pin kollidiert mit Foto-Eingang.";}
    else if(!servoEnabled){ledcSetup(SERVO_CHANNEL,50,16);ledcWrite(SERVO_CHANNEL,0);ledcAttachPin(servo.pin,SERVO_CHANNEL);servoEnabled=true;servoCurrent=servoTarget=0;servoProtocol.reset(digitalRead(servo.commandPin),millis());logEvent("Servo-Signal manuell freigegeben.");}
   }else if(action=="move"){
    if(!servoEnabled){code=409;message="Servo-Signal zuerst freigeben.";}
    else if(!d["angle"].is<float>()||!isfinite(d["angle"].as<float>())||d["angle"].as<float>()<0||d["angle"].as<float>()>servo.degrees){code=400;message="Winkel außerhalb des kalibrierten Bereichs.";}
    else{servoTarget=servo.minUs+(servo.maxUs-servo.minUs)*d["angle"].as<float>()/servo.degrees;logEvent("Servo-Ziel: "+String(servoTarget)+" us.");}
   }else{code=400;message="Unbekannte Servo-Aktion.";}
  }
  response(code,message);
 });
 web.onNotFound([]{response(404,"Nicht gefunden.");});
 const char *headers[]={"X-Shutter"};web.collectHeaders(headers,1);web.begin();
}
void setup(){
 Serial.begin(115200);guard=xSemaphoreCreateMutex();loadConfig();
 pinMode(engine.cfg.pin,engine.cfg.pullup?INPUT_PULLUP:INPUT);engine.input.reset(digitalRead(engine.cfg.pin),millis());
 configureNetwork();setupBLE();setupRoutes();
 {Lock lock;logEvent("System gestartet. Aufnahme deaktiviert.");}
 if(xTaskCreatePinnedToCore(captureTask,"capture",6144,nullptr,2,nullptr,1)!=pdPASS){Serial.println("Capture task failed");while(true)delay(1000);}
}
void loop(){
 web.handleClient();
 if(rebootAt&&due(millis(),rebootAt))ESP.restart();
 static uint32_t lastSerial=0;
 if(millis()-lastSerial>15000){lastSerial=millis();Serial.printf("STATUS BLE=%d ready=%d IP=%s\n",connected.load(),hidReady(),WiFi.localIP().toString().c_str());}
 delay(2);
}
