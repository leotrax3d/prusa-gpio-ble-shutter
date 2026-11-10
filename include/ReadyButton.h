#pragma once
#include "Trigger.h"
struct ReadyConfig {bool configured=false;uint8_t input=32,output=25;uint32_t debounce=30,pulse=1000,cooldown=2000;};
struct ReadyButton {
 ReadyConfig cfg;Trigger input{true,0};bool enabled=false,pulsing=false,hadPulse=false;
 uint32_t started=0,lastPulse=0,sent=0,skipped=0;
 void disable(){enabled=pulsing=false;}
 void enable(bool level,uint32_t now){disable();enabled=true;input.reset(level,now);}
 int tick(bool level,uint32_t now){
  if(pulsing&&uint32_t(now-started)>=cfg.pulse){pulsing=false;return -1;}
  if(input.update(level,now,false,cfg.debounce)&&enabled){
   if(pulsing||(hadPulse&&uint32_t(now-lastPulse)<cfg.cooldown)){skipped++;return 0;}
   pulsing=true;hadPulse=true;started=lastPulse=now;sent++;return 1;
  }
  return 0;
 }
};
