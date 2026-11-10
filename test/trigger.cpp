#include "Trigger.h"
#include <cassert>
int main(){
 Trigger t(true,0);
 assert(!t.update(true,30,false,30));
 assert(!t.update(false,40,false,30));
 assert(!t.update(true,45,false,30)); // bounce
 assert(!t.update(false,50,false,30));
 assert(t.update(false,80,false,30));
 assert(!t.update(false,500,false,30)); // held low, no repeats
 t.reset(false,600); assert(!t.update(false,900,false,30)); // no startup shot
 assert(!t.update(true,1000,false,30)); assert(!t.update(true,1030,false,30));
 assert(!t.update(false,1100,false,30)); assert(t.update(false,1130,false,30));
 Trigger wrap(false,0xfffffff0u); assert(!wrap.update(false,0x20u,true,30));
 assert(!wrap.update(true,0x30u,true,30));assert(wrap.update(true,0x50u,true,30));
}
