#include "ReadyButton.h"
#include <cassert>
int main(){
 ReadyButton b;b.enable(true,0);b.tick(true,30);b.tick(false,100);assert(b.tick(false,130)==1);
 assert(b.tick(false,1129)==0);assert(b.tick(false,1130)==-1);assert(b.tick(false,2000)==0);assert(b.sent==1);
 b.disable();assert(!b.enabled&&!b.pulsing);b.enable(false,3000);assert(b.tick(false,4000)==0); // held button cannot start after enable
 b.tick(true,4100);b.tick(true,4130);b.tick(false,4200);assert(b.tick(false,4230)==1);
 b.tick(true,4300);b.tick(true,4330);b.tick(false,4400);assert(b.tick(false,4430)==0);assert(b.skipped==1);
 b.disable();b.enable(true,0xffffff00u);b.tick(true,0xffffff30u);b.tick(false,0xffffff40u);assert(b.tick(false,0xffffff60u)==1);
 assert(b.tick(false,0x348u)==-1); // 1000 ms across rollover
}
