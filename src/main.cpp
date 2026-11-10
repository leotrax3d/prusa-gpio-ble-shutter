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
#include "LogBuffer.h"
#include "ReadyButton.h"
#include <driver/gpio.h>
#include <errno.h>
#include "web.h"

WebServer web(80);
Preferences prefs;
CaptureEngine engine;
SemaphoreHandle_t guard;
BLECharacteristic *report=nullptr;
BLE2902 *reportCccd=nullptr;
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
ReadyButton readyButton;
void configureReadyPins(){
 if(!readyButton.cfg.configured)return;
 pinMode(readyButton.cfg.input,INPUT_PULLUP);
 digitalWrite(readyButton.cfg.output,HIGH);pinMode(readyButton.cfg.output,OUTPUT_OPEN_DRAIN);
 gpio_pullup_en((gpio_num_t)readyButton.cfg.output);digitalWrite(readyButton.cfg.output,HIGH);
}
void stopReady(){readyButton.disable();if(readyButton.cfg.configured)digitalWrite(readyButton.cfg.output,HIGH);}
bool readyUses(uint8_t pin){return readyButton.cfg.configured&&(pin==readyButton.cfg.input||pin==readyButton.cfg.output);}
struct ServoConfig {bool automation=false;uint8_t commandPin=33;uint8_t pin=26;uint16_t degrees=270,minUs=1000,maxUs=2000,stepUs=10;} servo;
bool servoEnabled=false;
uint16_t servoCurrent=0,servoTarget=0;
uint32_t servoLast=0;
constexpr uint8_t SERVO_CHANNEL=6;
void disableServo(){if(servoEnabled){ledcWrite(SERVO_CHANNEL,0);ledcDetachPin(servo.pin);pinMode(servo.pin,INPUT);}servoEnabled=false;servoCurrent=servoTarget=0;servoProtocol.reset(digitalRead(servo.commandPin),millis());}
void logEvent(const char *text);
template<typename... Args> void logEventf(const char *format,Args... args);
struct TaskMetrics {uint32_t cycles=0,lastUs=0,maxUs=0,maxGapMs=0;};
TaskMetrics taskMetrics;
struct DeviceInfo {uint32_t flashSize=0,appSize=0,otaSpace=0,resetReason=0;};
DeviceInfo deviceInfo;
struct StatusSnapshot {
 bool connected,ready,armed,pending,held,updating;uint8_t level,pin,mode;
 uint32_t sent,skipped,pulses,filtered,session,nextMs;uint16_t remaining;
 bool servoEnabled;uint16_t servoTarget,servoCurrent;TaskMetrics metrics;
 bool readyConfigured,readyEnabled,readyPulsing;uint32_t readySent,readySkipped;
};
void servoTick(uint32_t now){
 if(servo.automation){
  int slot=servoProtocol.tick(digitalRead(servo.commandPin),now);
  if(slot&&servoEnabled&&slot<=ruleCount){
   servoTarget=servo.minUs+(uint32_t)(servo.maxUs-servo.minUs)*rules[slot-1].angle/servo.degrees;
   logEventf("Servo action %u: %u degrees.",(unsigned)slot,(unsigned)rules[slot-1].angle);
  }
 }
 if(!servoEnabled||!servoTarget||servoCurrent==servoTarget||uint32_t(now-servoLast)<20)return;
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
LogBuffer eventLog;
// The synchronous HTTP task owns this snapshot; no large log array on its stack.
LogBatch httpLogBatch;
void logEvent(const char *text){eventLog.append(millis(),"%s",text);}
void logEvent(const String &text){logEvent(text.c_str());}
template<typename... Args> void logEventf(const char *format,Args... args){eventLog.append(millis(),format,args...);}
void sendJson(JsonDocument &d,int code=200){String body;body.reserve(measureJson(d)+1);serializeJson(d,body);web.sendHeader("Cache-Control","no-store");web.send(code,"application/json",body);}
void response(int code,const String &message){JsonDocument d;d[code<400?"message":"error"]=message;sendJson(d,code);}
bool auth(){
 if(!web.authenticate("admin",adminPassword.c_str())){web.requestAuthentication();return false;}
 if(web.method()==HTTP_POST&&web.header("X-Shutter")!="1"){response(403,"Missing request header.");return false;}
 return true;
}
bool readJson(JsonDocument &d){
 if(web.arg("plain").length()>JSON_LIMIT){response(413,"Settings are too large.");return false;}
 if(deserializeJson(d,web.arg("plain"))||!d.is<JsonObject>()){response(400,"Expected a JSON object.");return false;}
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
 if(!d.is<JsonObjectConst>()){error="Expected a settings object.";return false;}
 if(d["schema"].isNull()||!d["schema"].is<int>()||d["schema"].as<int>()!=2){error="Settings require schema 2.";return false;}
 struct Field{const char *name;uint32_t low,high;};
 const Field fields[]={{"mode",0,2},{"key",0,2},{"pin",0,39},{"debounce",5,500},{"cooldown",100,60000},{"hold",30,1000},{"delay",0,600000},{"every",1,1000},{"burst",1,50},{"burstGap",100,60000},{"interval",200,86400000},{"startDelay",0,600000},{"maxFrames",0,1000000},{"autoStop",0,86400000}};
 for(auto &f:fields){if(!d[f.name].is<uint32_t>()||d[f.name].as<uint32_t>()<f.low||d[f.name].as<uint32_t>()>f.high){error=String("Invalid value: ")+f.name;return false;}}
 for(auto key:{"activeHigh","pullup","disarmOnDisconnect"})if(!d[key].is<bool>()){error=String("Expected true or false: ")+key;return false;}
 c.mode=d["mode"];c.key=d["key"];c.pin=d["pin"];
 if(c.pin!=13&&c.pin!=14&&c.pin!=25&&c.pin!=26&&c.pin!=27&&c.pin!=32&&c.pin!=33){error="Allowed GPIOs: 13, 14, 25, 26, 27, 32, 33.";return false;}
 c.activeHigh=d["activeHigh"];c.pullup=d["pullup"];c.disarmOnDisconnect=d["disarmOnDisconnect"];
 c.debounce=d["debounce"];c.cooldown=d["cooldown"];c.hold=d["hold"];c.delay=d["delay"];
 c.every=d["every"];c.burst=d["burst"];c.burstGap=d["burstGap"];c.interval=d["interval"];
 c.startDelay=d["startDelay"];c.maxFrames=d["maxFrames"];c.autoStop=d["autoStop"];
 if(c.cooldown<c.hold+30){error="Minimum spacing must be key hold + 30 ms or more.";return false;}
 if(c.burstGap<c.cooldown){error="Burst spacing must meet the minimum spacing.";return false;}
 if(c.mode==1&&c.interval<c.delay+(c.burst-1)*c.burstGap+c.cooldown){error="Interval is too short for this burst.";return false;}
 return true;
}
bool hidReady(){
 if(!connected.load()||!encrypted.load()||!report)return false;
 return reportCccd&&reportCccd->getNotifications();
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
 TickType_t lastWake=xTaskGetTickCount();uint32_t lastStart=millis();
 for(;;){
  const uint32_t startedUs=micros(),now=millis();uint32_t periodMs=10;
  {
   Lock lock;
   if(disconnected.exchange(false)){
    if(engine.cfg.disarmOnDisconnect)stopCapture();else {engine.pending=false;engine.remaining=0;}
    logEvent("Bluetooth disconnected. Pending shots dropped.");keyRelease();
    if(allowAdvertising.load())BLEDevice::startAdvertising();
   }
   if(!updating){
    if(readyButton.cfg.configured){
     const int change=readyButton.tick(digitalRead(readyButton.cfg.input),now);
     if(change>0){digitalWrite(readyButton.cfg.output,LOW);logEvent("Ready pulse sent. Printer state not confirmed.");}
     else if(change<0)digitalWrite(readyButton.cfg.output,HIGH);
    }
    servoTick(now);
    int action=engine.tick(digitalRead(engine.cfg.pin),hidReady(),now);
    if(action<0)keyRelease();
    if(action>0){uint8_t key=engine.cfg.key==1?2:(engine.cfg.key==2&&engine.sent%2==0?2:1);report->setValue(&key,1);report->notify();logEventf("Key command #%lu sent.",(unsigned long)engine.sent);}
    if(engine.armed||engine.pending||engine.held||readyButton.pulsing||(servoEnabled&&(servo.automation||servoCurrent!=servoTarget)))periodMs=2;
   }
   taskMetrics.cycles++;taskMetrics.lastUs=micros()-startedUs;
   taskMetrics.maxUs=max(taskMetrics.maxUs,taskMetrics.lastUs);
   taskMetrics.maxGapMs=max(taskMetrics.maxGapMs,uint32_t(now-lastStart));
  }
  lastStart=now;
  const TickType_t period=max((TickType_t)1,pdMS_TO_TICKS(periodMs));
  const TickType_t current=xTaskGetTickCount();
  if(current-lastWake>period)lastWake=current; // never run a catch-up loop after a stall
  vTaskDelayUntil(&lastWake,period);
 }
}
void setupBLE(){
 static uint8_t map[]={0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x01,0x15,0x00,0x25,0x01,0x09,0xe9,0x09,0xea,0x75,0x01,0x95,0x02,0x81,0x02,0x75,0x06,0x95,0x01,0x81,0x03,0xc0};
 BLEDevice::init(net.bleName.c_str());BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
 bleServer=BLEDevice::createServer();bleServer->setCallbacks(new ServerCallbacks());
 auto hid=new BLEHIDDevice(bleServer);report=hid->inputReport(1);
 reportCccd=static_cast<BLE2902*>(report->getDescriptorByUUID(BLEUUID((uint16_t)0x2902)));
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
 JsonDocument button;
 if(!deserializeJson(button,prefs.getString("ready2",""))){
  int in=button["input"]|32,out=button["output"]|25;
  uint32_t debounce=button["debounce"]|30u,pulse=button["pulse"]|1000u,cooldown=button["cooldown"]|2000u;
  auto pinOK=[](int n){return n==13||n==14||n==25||n==26||n==27||n==32||n==33;};
  if(pinOK(in)&&pinOK(out)&&in!=out&&in!=engine.cfg.pin&&out!=engine.cfg.pin&&in!=servo.pin&&out!=servo.pin&&(!servo.automation||(in!=servo.commandPin&&out!=servo.commandPin))&&debounce>=10&&debounce<=200&&pulse>=700&&pulse<=2000&&cooldown>=pulse+500&&cooldown<=10000){
   readyButton.cfg.configured=true;readyButton.cfg.input=in;readyButton.cfg.output=out;readyButton.cfg.debounce=debounce;readyButton.cfg.pulse=pulse;readyButton.cfg.cooldown=cooldown;
  }
 }
 configureReadyPins();

 if(servo.automation)pinMode(servo.commandPin,INPUT_PULLUP);servoProtocol.reset(digitalRead(servo.commandPin),millis());
}
void configureNetwork(){
 WiFi.mode(net.ssid.length()?WIFI_AP_STA:WIFI_AP);WiFi.setHostname(net.hostname.c_str());
 WiFi.softAP(net.ap.c_str(),net.password.c_str(),net.channel,false,4);
 if(net.ssid.length())WiFi.begin(net.ssid.c_str(),net.wifiPassword.c_str());
 if(MDNS.begin(net.hostname.c_str()))MDNS.addService("http","tcp",80);
 Serial.printf("\nPrusa Shutter %s\nWi-Fi: %s\nWi-Fi password: %s\nDashboard: http://192.168.4.1\nLogin: admin / %s\n",FIRMWARE_VERSION,net.ap.c_str(),net.password.c_str(),adminPassword.c_str());
}
void scheduleReboot(){rebootAt=millis()+1500;}
void setupRoutes(){
 web.on("/",HTTP_GET,[]{if(!auth())return;web.sendHeader("Cache-Control","no-cache");web.sendHeader("ETag","\"" FIRMWARE_VERSION "\"");if(web.header("If-None-Match")=="\"" FIRMWARE_VERSION "\""){web.send(304);return;}web.sendHeader("Content-Encoding","gzip");web.send_P(200,"text/html; charset=utf-8",(const char*)PAGE,sizeof(PAGE));});
 web.on("/api/status",HTTP_GET,[]{
  if(!auth())return;StatusSnapshot v;
  {
   Lock lock;const uint32_t now=millis();
   v.connected=connected.load();v.ready=hidReady();v.armed=engine.armed;v.pending=engine.pending;v.held=engine.held;
   v.level=digitalRead(engine.cfg.pin);v.pin=engine.cfg.pin;v.mode=engine.cfg.mode;
   v.sent=engine.sent;v.skipped=engine.skipped;v.pulses=engine.pulses;v.filtered=engine.filtered;v.session=engine.session;v.remaining=engine.remaining;
   v.nextMs=engine.pending?max(0,(int32_t)(engine.next-now)):(engine.armed&&engine.cfg.mode==1?max(0,(int32_t)(engine.nextInterval-now)):0);
   v.updating=updating;v.servoEnabled=servoEnabled;v.servoTarget=servoTarget;v.servoCurrent=servoCurrent;v.metrics=taskMetrics;
   v.readyConfigured=readyButton.cfg.configured;v.readyEnabled=readyButton.enabled;v.readyPulsing=readyButton.pulsing;v.readySent=readyButton.sent;v.readySkipped=readyButton.skipped;
  }
  // All JSON allocations and network calls happen after releasing the capture mutex.
  JsonDocument d;d["version"]=FIRMWARE_VERSION;d["connected"]=v.connected;d["ready"]=v.ready;d["armed"]=v.armed;
  d["pending"]=v.pending;d["held"]=v.held;d["level"]=v.level;d["pin"]=v.pin;d["mode"]=v.mode;
  d["sent"]=v.sent;d["skipped"]=v.skipped;d["pulses"]=v.pulses;d["filtered"]=v.filtered;d["session"]=v.session;d["remaining"]=v.remaining;d["nextMs"]=v.nextMs;d["updating"]=v.updating;
  d["servo"]["enabled"]=v.servoEnabled;d["servo"]["targetUs"]=v.servoTarget;d["servo"]["currentUs"]=v.servoCurrent;
  d["readyButton"]["configured"]=v.readyConfigured;d["readyButton"]["enabled"]=v.readyEnabled;d["readyButton"]["pulsing"]=v.readyPulsing;d["readyButton"]["sent"]=v.readySent;d["readyButton"]["skipped"]=v.readySkipped;
  d["taskLastUs"]=v.metrics.lastUs;d["taskMaxUs"]=v.metrics.maxUs;d["taskMaxGapMs"]=v.metrics.maxGapMs;d["taskCycles"]=v.metrics.cycles;
  d["uptime"]=millis()/1000;d["freeHeap"]=ESP.getFreeHeap();d["minHeap"]=ESP.getMinFreeHeap();
  d["flashSize"]=deviceInfo.flashSize;d["appSize"]=deviceInfo.appSize;d["otaSpace"]=deviceInfo.otaSpace;d["resetReason"]=deviceInfo.resetReason;
  const bool wifi=WiFi.status()==WL_CONNECTED;
  d["wifiConnected"]=wifi;d["ip"]=wifi?WiFi.localIP().toString():"";d["rssi"]=wifi?WiFi.RSSI():0;
  d["apIP"]=WiFi.softAPIP().toString();d["clients"]=WiFi.softAPgetStationNum();d["rebootRequired"]=rebootRequired;
  d["bonds"]=esp_ble_get_bond_device_num();sendJson(d);
 });
 web.on("/api/config",HTTP_GET,[]{if(!auth())return;CaptureConfig c;{Lock lock;c=engine.cfg;}JsonDocument d;configJson(d,c);sendJson(d);});
 web.on("/api/config",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;CaptureConfig c;String error;
  if(!parseConfig(d.as<JsonVariantConst>(),c,error)){response(400,error);return;}
  bool pinConflict;{Lock lock;pinConflict=readyUses(c.pin)||(c.pin==servo.pin&&servoEnabled)||(servo.automation&&c.pin==servo.commandPin);}
  if(pinConflict){response(409,"Capture input conflicts with a servo or button pin.");return;}
  String saved;serializeJson(d,saved);
  // Persist first; never block the timing task with a flash write under the mutex.
  if(!prefs.putString("config2",saved)){response(500,"Save failed.");return;}
  {Lock lock;stopCapture();pinMode(engine.cfg.pin,INPUT);pinMode(c.pin,c.pullup?INPUT_PULLUP:INPUT);engine.configure(c,digitalRead(c.pin),millis());logEvent("Settings saved. Capture stopped.");}
  response(200,"Saved. Capture stopped.");
 });
 web.on("/api/action",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;String action=d["action"]|"";
  int code=200;String message="Done.";
  {
   Lock lock;
   if(updating){code=409;message="Update in progress.";}
   else if(action=="start"){
    if(!hidReady()){code=409;message="Pair your phone and wait for Bluetooth ready.";}
    else{stopCapture();engine.start(digitalRead(engine.cfg.pin),millis());logEvent("Capture started.");}
   }else if(action=="stop"){stopCapture();logEvent("Capture stopped.");}
   else if(action=="stopAll"){stopCapture();disableServo();stopReady();logEvent("All outputs stopped.");}
   else if(action=="test"){
    if(!engine.request(millis(),hidReady(),true)){code=409;message="Not ready, shot pending, or minimum spacing active.";}
   }else if(action=="resetCounters"){
    if(engine.armed||engine.pending||engine.held){code=409;message="Stop capture first.";}
    else{engine.resetCounters();logEvent("Counters cleared.");}
   }else if(action=="clearLog")eventLog.clear();
   else{code=400;message="Unknown action.";}
  }
  response(code,message);
 });
 web.on("/api/logs",HTTP_GET,[]{
  if(!auth())return;
  uint64_t after=0;uint32_t generation=0;
  if(web.hasArg("after")){
   String value=web.arg("after");char *end=nullptr;errno=0;
   if(value.length()==0||value.length()>20||value[0]=='-'||value[0]=='+'||!isdigit((unsigned char)value[0])){response(400,"Invalid log cursor.");return;}
   after=strtoull(value.c_str(),&end,10);
   if(errno||*end){response(400,"Invalid log cursor.");return;}
  }
  if(web.hasArg("generation")){
   String value=web.arg("generation");char *end=nullptr;errno=0;
   unsigned long long parsed=strtoull(value.c_str(),&end,10);
   if(value.length()==0||value.length()>10||!isdigit((unsigned char)value[0])||errno||*end||parsed>UINT32_MAX){response(400,"Invalid log generation.");return;}
   generation=(uint32_t)parsed;
  }
  {Lock lock;eventLog.snapshot(httpLogBatch,after,generation);}
  JsonDocument d;d["generation"]=httpLogBatch.generation;d["reset"]=httpLogBatch.reset;
  char cursor[24];snprintf(cursor,sizeof(cursor),"%llu",(unsigned long long)httpLogBatch.cursor);d["cursor"]=cursor;
  auto list=d["events"].to<JsonArray>();
  for(size_t i=0;i<httpLogBatch.count;i++){
   const auto &item=httpLogBatch.entries[i];auto e=list.add<JsonObject>();
   char id[24];snprintf(id,sizeof(id),"%llu",(unsigned long long)item.id);e["id"]=id;e["ms"]=item.ms;e["text"]=item.text;
  }
  sendJson(d);
 });
 web.on("/api/network",HTTP_GET,[]{if(!auth())return;JsonDocument d;d["ap"]=net.ap;d["ssid"]=net.ssid;d["hostname"]=net.hostname;d["bleName"]=net.bleName;d["channel"]=net.channel;d["hasWifiPassword"]=net.wifiPassword.length()>0;sendJson(d);});
 web.on("/api/network",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  for(auto k:{"ap","ssid","hostname","bleName"})if(!d[k].is<String>()){response(400,"Network names are missing.");return;}
  String ap=d["ap"],ssid=d["ssid"],host=d["hostname"],ble=d["bleName"];
  if(ap.length()<1||ap.length()>32||ssid.length()>32||host.length()<1||host.length()>24||ble.length()<1||ble.length()>24||!d["channel"].is<int>()||d["channel"].as<int>()<1||d["channel"].as<int>()>11){response(400,"Name or channel out of range.");return;}
  for(unsigned i=0;i<host.length();i++)if(!isalnum((unsigned char)host[i])&&host[i]!='-'){response(400,"Hostname: letters, numbers and hyphens only.");return;}
  if(host[0]=='-'||host[host.length()-1]=='-'){response(400,"Hostname cannot start or end with a hyphen.");return;}
  String pass=d["password"]|"",admin=d["adminPassword"]|"",wifi=d["wifiPassword"]|"";
  if((pass.length()&&(pass.length()<8||pass.length()>63))||(admin.length()&&(admin.length()<8||admin.length()>63))||wifi.length()>63||(wifi.length()>0&&wifi.length()<8)){response(400,"Passwords: 8–63 characters. Empty keeps current.");return;}
  {Lock lock;stopCapture();}
  bool ok=true;
  auto save=[&](const char*k,const String&v){if(prefs.getString(k,"\x01")!=v){prefs.putString(k,v);ok=(prefs.getString(k,"\x01")==v)&&ok;}};
  save("ap",ap);save("ssid",ssid);save("host",host);save("ble",ble);
  uint8_t ch=d["channel"];if(net.channel!=ch)ok=prefs.putUChar("channel",ch)>0&&ok;
  if(pass.length())save("password",pass);if(admin.length())save("admin",admin);
  if(d["clearWifiPassword"]==true)save("wifipass","");else if(wifi.length())save("wifipass",wifi);
  if(!ok){response(500,"Network save failed. Check settings.");return;}
  // Keep current credentials until reboot, so users can finish their session.
  rebootRequired=true;response(200,"Saved. Restart to apply.");
 });
 web.on("/api/system",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;String action=d["action"]|"";
  if(d["confirm"]!="BESTAETIGEN"){response(400,"Confirmation required.");return;}
  if(action!="reboot"&&action!="forgetBonds"&&action!="resetSettings"){response(400,"Unknown system action.");return;}
  {Lock lock;stopCapture();disableServo();stopReady();logEvent("System action: "+action);}
  if(action=="forgetBonds"){
   // Stop reconnects while bonds are being deleted; reboot after API response.
   allowAdvertising=false;BLEDevice::getAdvertising()->stop();if(connected.load())bleServer->disconnect(bleServer->getConnId());
   int count=esp_ble_get_bond_device_num();
   if(count>0){auto bonds=(esp_ble_bond_dev_t*)calloc(count,sizeof(esp_ble_bond_dev_t));if(!bonds){response(500,"Not enough memory.");return;}esp_ble_get_bond_device_list(&count,bonds);for(int i=0;i<count;i++)esp_ble_remove_bond_device(bonds[i].bd_addr);free(bonds);}
  }
  if(action=="resetSettings"){prefs.remove("config2");for(auto key:{"high","down","debounce","cooldown","hold"})prefs.remove(key);}
  response(200,"Restarting. Capture stays off.");scheduleReboot();
 });
 web.on("/api/update",HTTP_POST,[]{
  if(!auth())return;
  if(!uploadAllowed){response(400,"No valid firmware upload.");return;}
  uploadAllowed=false;
  if(uploadOK){response(200,"Updated. Restarting.");scheduleReboot();}
  else {response(400,"Update failed. Use the correct firmware.bin.");}
 },[]{
  auto &upload=web.upload();
  if(upload.status==UPLOAD_FILE_START){
   uploadAllowed=web.authenticate("admin",adminPassword.c_str())&&web.header("X-Shutter")=="1";
   uploadOK=false;uploadedBytes=0;if(!uploadAllowed)return;
   {Lock lock;stopCapture();disableServo();stopReady();updating=true;logEvent("Firmware upload started.");}
   if(!upload.filename.endsWith(".bin")||!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){uploadAllowed=false;Lock lock;updating=false;}
  }else if(upload.status==UPLOAD_FILE_WRITE&&uploadAllowed){
   // Application images contain esp_app_desc_t magic at byte 32. Bootloader/merged images do not.
   if(uploadedBytes==0&&(upload.currentSize<36||upload.buf[0]!=0xe9||upload.buf[12]!=0||upload.buf[13]!=0||upload.buf[32]!=0x32||upload.buf[33]!=0x54||upload.buf[34]!=0xcd||upload.buf[35]!=0xab)){
    Update.abort();uploadAllowed=false;Lock lock;updating=false;return;
   }
   if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize){Update.abort();uploadAllowed=false;Lock lock;updating=false;}
   uploadedBytes+=upload.currentSize;
  }else if(upload.status==UPLOAD_FILE_END&&uploadAllowed){
   uploadOK=Update.end(true);Lock lock;updating=false;logEvent(uploadOK?"Update complete.":"Update failed.");
  }else if(upload.status==UPLOAD_FILE_ABORTED){Update.abort();uploadAllowed=false;Lock lock;updating=false;}
 });

 web.on("/api/servo/config",HTTP_GET,[]{
  if(!auth())return;ServoConfig c;uint8_t count;
  struct RuleView{char when[16];uint32_t layer;uint16_t angle;};RuleView view[8];
  {Lock lock;c=servo;count=ruleCount;for(uint8_t i=0;i<count;i++){snprintf(view[i].when,sizeof(view[i].when),"%s",rules[i].when.c_str());view[i].layer=rules[i].layer;view[i].angle=rules[i].angle;}}
  JsonDocument d;d["pin"]=c.pin;d["degrees"]=c.degrees;d["minUs"]=c.minUs;d["maxUs"]=c.maxUs;d["stepUs"]=c.stepUs;d["automation"]=c.automation;d["commandPin"]=c.commandPin;
  auto list=d["rules"].to<JsonArray>();for(uint8_t i=0;i<count;i++){auto item=list.add<JsonObject>();item["when"]=view[i].when;item["layer"]=view[i].layer;item["angle"]=view[i].angle;}
  sendJson(d);
 });
 web.on("/api/servo/config",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  for(auto k:{"pin","degrees","minUs","maxUs","stepUs","commandPin"})if(!d[k].is<uint16_t>()){response(400,"Invalid servo settings.");return;}
  uint16_t p=d["pin"],deg=d["degrees"],lo=d["minUs"],hi=d["maxUs"],step=d["stepUs"],command=d["commandPin"];
  if((p!=13&&p!=14&&p!=25&&p!=26&&p!=27&&p!=32&&p!=33)||(deg!=180&&deg!=270)||lo<500||hi>2500||hi<lo+100||step<1||step>100){response(400,"Check servo pin, pulse limits and step.");return;}
  if(!d["automation"].is<bool>()||!d["rules"].is<JsonArray>()||d["rules"].size()>8||(command!=13&&command!=14&&command!=25&&command!=26&&command!=27&&command!=32&&command!=33)||command==p){response(400,"Check command input and plan. Limit: 8 actions.");return;}
  for(JsonObject r:d["rules"].as<JsonArray>()){
   String when=r["when"]|"";
   if((when!="start"&&when!="layer"&&when!="layerNumber"&&when!="end")||!r["angle"].is<uint16_t>()||r["angle"].as<uint16_t>()>deg||!r["layer"].is<uint32_t>()||r["layer"].as<uint32_t>()>100000){response(400,"Invalid servo action.");return;}
  }
  bool conflict;{Lock lock;conflict=readyUses(p)||(d["automation"].as<bool>()&&readyUses(command))||p==engine.cfg.pin||(d["automation"].as<bool>()&&command==engine.cfg.pin);if(!conflict)disableServo();}
  if(conflict){response(409,"Capture, servo and button need separate pins.");return;}
  String stored;serializeJson(d,stored);
  if(!prefs.putString("servo2",stored)){response(500,"Servo save failed.");return;}
  {Lock lock;if(servo.automation)pinMode(servo.commandPin,INPUT);servo.pin=p;servo.degrees=deg;servo.minUs=lo;servo.maxUs=hi;servo.stepUs=step;servo.automation=d["automation"];servo.commandPin=command;ruleCount=0;
   for(JsonObject r:d["rules"].as<JsonArray>()){rules[ruleCount].when=r["when"].as<String>();rules[ruleCount].layer=r["layer"];rules[ruleCount].angle=r["angle"];ruleCount++;}
   if(servo.automation)pinMode(command,INPUT_PULLUP);servoProtocol.reset(digitalRead(command),millis());logEvent("Servo plan saved. Signal off.");}
  response(200,"Saved. Servo signal off.");
 });
 web.on("/api/servo/action",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  String action=d["action"]|"";int code=200;String message="Servo command set.";
  {
   Lock lock;
   if(updating){code=409;message="Update in progress.";}
   else if(action=="disable"){disableServo();logEvent("Servo signal off.");}
   else if(d["confirm"]!="BESTAETIGEN"){code=400;message="Confirmation required.";}
   else if(action=="enable"){
    if(servo.pin==engine.cfg.pin){code=409;message="Servo pin conflicts with capture input.";}
    else if(!servoEnabled){ledcSetup(SERVO_CHANNEL,50,16);ledcWrite(SERVO_CHANNEL,0);ledcAttachPin(servo.pin,SERVO_CHANNEL);servoEnabled=true;servoCurrent=servoTarget=0;servoProtocol.reset(digitalRead(servo.commandPin),millis());logEvent("Servo signal enabled.");}
   }else if(action=="move"){
    if(!servoEnabled){code=409;message="Enable the servo signal first.";}
    else if(!d["angle"].is<float>()||!isfinite(d["angle"].as<float>())||d["angle"].as<float>()<0||d["angle"].as<float>()>servo.degrees){code=400;message="Angle outside calibrated range.";}
    else{servoTarget=servo.minUs+(servo.maxUs-servo.minUs)*d["angle"].as<float>()/servo.degrees;logEventf("Servo target: %u us.",(unsigned)servoTarget);}
   }else{code=400;message="Unknown servo action.";}
  }
  response(code,message);
 });
 web.on("/api/ready/config",HTTP_GET,[]{
  if(!auth())return;ReadyConfig c;{Lock lock;c=readyButton.cfg;}
  JsonDocument d;d["configured"]=c.configured;d["input"]=c.input;d["output"]=c.output;d["debounce"]=c.debounce;d["pulse"]=c.pulse;d["cooldown"]=c.cooldown;sendJson(d);
 });
 web.on("/api/ready/config",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;
  for(auto k:{"input","output","debounce","pulse","cooldown"})if(!d[k].is<uint32_t>()){response(400,"Invalid button settings.");return;}
  uint32_t in=d["input"],out=d["output"],debounce=d["debounce"],pulse=d["pulse"],cooldown=d["cooldown"];
  auto pinOK=[](uint32_t n){return n==13||n==14||n==25||n==26||n==27||n==32||n==33;};
  if(!pinOK(in)||!pinOK(out)||in==out||debounce<10||debounce>200||pulse<700||pulse>2000||cooldown<pulse+500||cooldown>10000){response(400,"Check pins and timing. Pulse: 700–2000 ms. Spacing: pulse + 500 ms or more.");return;}
  bool conflict;{Lock lock;conflict=in==engine.cfg.pin||out==engine.cfg.pin||in==servo.pin||out==servo.pin||(servo.automation&&(in==servo.commandPin||out==servo.commandPin));}
  if(conflict){response(409,"Button pins conflict with capture or servo.");return;}
  String stored;serializeJson(d,stored);if(prefs.getString("ready2","")!=stored&&!prefs.putString("ready2",stored)){response(500,"Button save failed.");return;}
  {Lock lock;stopReady();if(readyButton.cfg.configured)pinMode(readyButton.cfg.input,INPUT);readyButton.cfg.configured=true;readyButton.cfg.input=in;readyButton.cfg.output=out;readyButton.cfg.debounce=debounce;readyButton.cfg.pulse=pulse;readyButton.cfg.cooldown=cooldown;configureReadyPins();logEvent("Ready button saved. Disabled.");}
  response(200,"Saved. Ready button disabled.");
 });
 web.on("/api/ready/action",HTTP_POST,[]{
  if(!auth())return;JsonDocument d;if(!readJson(d))return;String action=d["action"]|"";int code=200;String message="Done.";
  {Lock lock;
   if(action=="disable"){stopReady();logEvent("Ready button disabled.");}
   else if(action=="enable"){
    if(updating||!readyButton.cfg.configured){code=409;message="Save button settings first.";}
    else if(d["confirm"]!="BESTAETIGEN"){code=400;message="Confirmation required.";}
    else{readyButton.enable(digitalRead(readyButton.cfg.input),millis());logEvent("Ready button enabled.");}
   }else{code=400;message="Unknown button action.";}
  }
  response(code,message);
 });
 web.onNotFound([]{response(404,"Not found.");});
 const char *headers[]={"X-Shutter","If-None-Match"};web.collectHeaders(headers,2);web.begin();
}
void setup(){
 Serial.begin(115200);guard=xSemaphoreCreateMutex();eventLog.seed(esp_random());loadConfig();
 deviceInfo.flashSize=ESP.getFlashChipSize();deviceInfo.appSize=ESP.getSketchSize();deviceInfo.otaSpace=ESP.getFreeSketchSpace();deviceInfo.resetReason=(uint32_t)esp_reset_reason();
 pinMode(engine.cfg.pin,engine.cfg.pullup?INPUT_PULLUP:INPUT);engine.input.reset(digitalRead(engine.cfg.pin),millis());
 configureNetwork();setupBLE();setupRoutes();
 {Lock lock;logEvent("Started. Capture off.");}
 if(xTaskCreatePinnedToCore(captureTask,"capture",6144,nullptr,2,nullptr,1)!=pdPASS){Serial.println("Capture task failed");while(true)delay(1000);}
}
void loop(){
 web.handleClient();
 if(rebootAt&&due(millis(),rebootAt))ESP.restart();
 static uint32_t lastSerial=0;
 if(millis()-lastSerial>15000){lastSerial=millis();Serial.printf("STATUS BLE=%d ready=%d IP=%s\n",connected.load(),hidReady(),WiFi.localIP().toString().c_str());}
 delay(2);
}
