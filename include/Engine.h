#pragma once
#include <stdint.h>
#include "Trigger.h"
// No Arduino dependencies: the same scheduler is exercised by native tests.
struct CaptureConfig {
 uint32_t debounce=30, cooldown=1000, hold=80, delay=0, burstGap=1200;
 uint32_t interval=5000, startDelay=3000, maxFrames=0, autoStop=0;
 uint16_t every=1, burst=1;
 uint8_t pin=27, mode=0, key=0; // mode input / interval / manual; key up/down/alternating
 bool activeHigh=true, pullup=true, disarmOnDisconnect=true;
};
inline bool due(uint32_t now,uint32_t deadline) {return int32_t(now-deadline)>=0;}
struct CaptureEngine {
 CaptureConfig cfg;
 Trigger input{true,0};
 bool armed=false, pending=false, held=false, hadShot=false;
 uint32_t next=0, nextInterval=0, pressed=0, lastShot=0, started=0;
 uint32_t sent=0, skipped=0, pulses=0, filtered=0, session=0, sequences=0;
 uint16_t remaining=0;
 void stop() {armed=false;pending=false;remaining=0;}
 void configure(const CaptureConfig &c,bool level,uint32_t now) {
  stop();cfg=c;input.reset(level,now);
 }
 void start(bool level,uint32_t now) {
  stop();armed=true;session=0;sequences=0;started=now;
  nextInterval=now+cfg.startDelay;input.reset(level,now);
 }
 bool request(uint32_t now,bool ready,bool test=false) {
  if((!test&&!armed)||!ready||pending||held||(hadShot&&uint32_t(now-lastShot)<cfg.cooldown)) {skipped++;return false;}
  if(!test&&cfg.maxFrames&&session>=cfg.maxFrames) {stop();skipped++;return false;}
  pending=true;remaining=test?1:cfg.burst;next=now+(test?0:cfg.delay);return true;
 }
 // Returns +1 for key press, -1 for release, 0 otherwise. BLE is outside this class.
 int tick(bool level,bool ready,uint32_t now) {
  if(held&&(!ready||uint32_t(now-pressed)>=cfg.hold)) {held=false;return -1;}
  if(!ready) {pending=false;remaining=0;if(cfg.disarmOnDisconnect)armed=false;}
  if(armed&&cfg.autoStop&&uint32_t(now-started)>=cfg.autoStop)stop();
  bool edge=input.update(level,now,cfg.activeHigh,cfg.debounce);
  if(edge&&armed&&cfg.mode==0) {
   pulses++;sequences++;
   if((sequences-1)%cfg.every==0) request(now,ready);else filtered++;
  }
  if(armed&&cfg.mode==1&&due(now,nextInterval)) {
   // Schedule from now: no catch-up storm after missed intervals.
   nextInterval=now+cfg.interval;request(now,ready);
  }
  if(pending&&due(now,next)) {
   if(!ready) {pending=false;remaining=0;skipped++;return 0;}
   if(cfg.maxFrames&&armed&&session>=cfg.maxFrames) {stop();return 0;}
   if(hadShot&&uint32_t(now-lastShot)<cfg.cooldown)return 0;
   held=true;pressed=lastShot=now;hadShot=true;sent++;session++;
   if(--remaining)next=now+cfg.burstGap;else pending=false;
   if(armed&&cfg.maxFrames&&session>=cfg.maxFrames)stop();
   return 1;
  }
  return 0;
 }
 void resetCounters(){sent=skipped=pulses=filtered=session=sequences=0;}
};
