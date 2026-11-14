#include "Engine.h"
#include <cassert>
void edge(CaptureEngine &e,uint32_t t){e.tick(false,true,t);e.tick(false,true,t+40);e.tick(true,true,t+50);}
int main(){
 CaptureEngine e;e.start(false,0);
 assert(e.tick(false,true,30)==0);assert(e.tick(true,true,40)==0);assert(e.tick(true,true,70)==1);
 assert(e.tick(true,true,100)==0);assert(e.tick(true,true,150)==-1);assert(e.tick(true,true,2000)==0);
 e.stop();assert(!e.armed&&!e.pending);
 // Burst obeys cooldown, counts session and never exceeds frame limit.
 CaptureConfig c;c.mode=2;c.burst=3;c.maxFrames=2;c.cooldown=100;c.hold=30;c.burstGap=150;e=CaptureEngine();e.configure(c,false,0);e.start(false,0);
 assert(e.request(0,true));assert(e.tick(false,true,0)==1);assert(e.tick(false,true,30)==-1);assert(e.tick(false,true,150)==1);assert(!e.armed&&!e.pending);assert(e.sent==2);assert(e.tick(false,true,180)==-1);
 // Countdown, auto-stop and no catch-up storm.
 c=CaptureConfig();c.mode=1;c.startDelay=1000;c.interval=2000;c.autoStop=4000;e=CaptureEngine();e.configure(c,false,0);e.start(false,0);
 assert(e.tick(false,true,999)==0);assert(e.tick(false,true,1000)==1);assert(e.tick(false,true,1080)==-1);assert(e.tick(false,true,3000)==1);assert(e.tick(false,true,3080)==-1);assert(e.tick(false,true,4000)==0);assert(!e.armed);
 // Every N: first, N+1; no repeat on held level.
 c=CaptureConfig();c.every=2;c.cooldown=100;c.hold=30;e=CaptureEngine();e.configure(c,false,0);e.start(false,0);
 e.tick(false,true,30);e.tick(true,true,40);assert(e.tick(true,true,70)==1);e.tick(true,true,100);
 edge(e,200);assert(e.tick(true,true,280)==0);assert(e.filtered==1);
 edge(e,400);assert(e.tick(true,true,480)==1);
 // Disconnect drops pending series, optional disarm behavior; no delayed replay.
 c=CaptureConfig();c.delay=1000;e=CaptureEngine();e.configure(c,false,0);e.start(false,0);assert(e.request(0,true));e.tick(false,false,100);assert(!e.pending&&!e.armed);assert(e.tick(false,true,2000)==0);
 c.disarmOnDisconnect=false;e=CaptureEngine();e.configure(c,false,0);e.start(false,0);e.tick(false,false,100);assert(e.armed&&!e.pending);
 // Deadlines and hold release across uint32 rollover.
 c=CaptureConfig();c.mode=1;c.startDelay=100;c.hold=30;c.cooldown=100;e=CaptureEngine();e.configure(c,false,0xffffffd0u);e.start(false,0xffffffd0u);assert(e.tick(false,true,0x33u)==0);assert(e.tick(false,true,0x34u)==1);assert(e.tick(false,true,0x52u)==-1);
}
