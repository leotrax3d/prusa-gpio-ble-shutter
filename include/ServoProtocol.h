#pragma once
#include <stdint.h>
// OUT6 command: stable LOW >=300 ms, HIGH slot*200 ms (+/-70), then LOW >=30 ms.
// Slot is 1..8. No execution on startup HIGH, stuck HIGH, short glitches or invalid widths.
struct ServoProtocol {
 bool raw=false,stable=false,ready=false,measuring=false;
 uint32_t changed=0,lowAt=0,highAt=0;
 uint8_t candidate=0;
 void reset(bool level,uint32_t now){raw=stable=level;changed=lowAt=now;ready=measuring=false;candidate=0;}
 int tick(bool level,uint32_t now){
  if(level!=raw){raw=level;changed=now;}
  if(raw!=stable&&uint32_t(now-changed)>=20){
   stable=raw;
   if(stable){
    if(ready){highAt=changed;measuring=true;}else measuring=false;
    ready=false;candidate=0;
   }else{
    lowAt=changed;
    if(measuring){uint32_t width=uint32_t(changed-highAt);uint32_t slot=(width+100)/200;
     if(slot>=1&&slot<=8&&width>=slot*200-70&&width<=slot*200+70)candidate=slot;
    }
    measuring=false;
   }
  }
  if(measuring&&uint32_t(now-highAt)>1800){measuring=false;candidate=0;}
  if(!stable&&!raw){
   if(candidate&&uint32_t(now-lowAt)>=30){uint8_t value=candidate;candidate=0;return value;}
   if(uint32_t(now-lowAt)>=300)ready=true;
  }
  return 0;
 }
};
