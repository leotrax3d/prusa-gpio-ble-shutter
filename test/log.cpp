#include "LogBuffer.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <new>
static size_t allocations=0;
void *operator new(size_t n){allocations++;if(void *p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
int main(){
 LogBuffer log;LogBatch batch;log.seed(42);
 const size_t before=allocations;
 for(unsigned i=1;i<=200;i++)log.append(i,"Shot %u",i);
 log.snapshot(batch,0,0);assert(batch.reset&&batch.count==48&&batch.cursor==200);
 assert(batch.entries[0].id==153&&batch.entries[47].id==200);
 assert(!std::strcmp(batch.entries[47].text,"Shot 200"));
 log.snapshot(batch,200,42);assert(!batch.reset&&batch.count==0);
 log.append(201,"New");log.snapshot(batch,200,42);assert(!batch.reset&&batch.count==1&&batch.entries[0].id==201);
 log.snapshot(batch,999,42);assert(batch.reset&&batch.count==48); // stale cursor after reboot
 log.snapshot(batch,1,42);assert(batch.reset&&batch.count==48); // client fell behind
 char longText[200];std::memset(longText,'x',199);longText[199]=0;
 log.append(202,"%s",longText);log.snapshot(batch,201,42);assert(batch.count==1&&std::strlen(batch.entries[0].text)==79);
 log.clear();log.snapshot(batch,202,42);assert(batch.reset&&batch.count==0&&batch.cursor==0&&batch.generation==43);
 log.append(1,"After clear");log.snapshot(batch,0,43);assert(!batch.reset&&batch.count==1);
 assert(allocations==before); // append, wrap, truncation and snapshots perform zero C++ heap allocations
}
