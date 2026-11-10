#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <type_traits>

struct LogEntry {uint64_t id=0;uint32_t ms=0;char text[80]={};};
struct LogBatch {
 static constexpr size_t capacity=48;
 LogEntry entries[capacity];size_t count=0;uint64_t cursor=0;uint32_t generation=0;bool reset=false;
};
class LogBuffer {
 LogEntry entries[LogBatch::capacity];size_t used=0,writeIndex=0;
 uint64_t sequence=0;uint32_t generation=1;
public:
 void seed(uint32_t value){generation=value?value:1;}
 void clear(){used=writeIndex=0;sequence=0;if(++generation==0)generation=1;}
 template<typename... Args> void append(uint32_t ms,const char *format,Args... args){
  LogEntry &e=entries[writeIndex];e.id=++sequence;e.ms=ms;
  snprintf(e.text,sizeof(e.text),format,args...);e.text[sizeof(e.text)-1]=0;
  writeIndex=(writeIndex+1)%LogBatch::capacity;if(used<LogBatch::capacity)used++;
 }
 void snapshot(LogBatch &out,uint64_t after,uint32_t clientGeneration)const{
  out.count=0;out.cursor=sequence;out.generation=generation;
  out.reset=clientGeneration!=generation||after>sequence;
  const size_t first=(writeIndex+LogBatch::capacity-used)%LogBatch::capacity;
  if(used&&after<entries[first].id-1)out.reset=true;
  if(out.reset)after=0;
  for(size_t n=0;n<used;n++){
   const LogEntry &e=entries[(first+n)%LogBatch::capacity];
   if(e.id>after)out.entries[out.count++]=e;
  }
 }
};
static_assert(std::is_trivially_copyable<LogEntry>::value,"Log entries must not allocate");
