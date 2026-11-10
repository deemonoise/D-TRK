#pragma once
#include <cstdint>
namespace engine {
enum class Cmd {ReleaseTies,SetBpm,Stop,SelectPattern,QueuePattern};
struct Status {bool playing=false,paused=false,song=false;uint8_t pattern=0;int queued=-1;uint8_t countIn=0;};
inline Status current;
inline int stops=0;
inline void lockProject(){}
inline void unlockProject(){}
inline bool post(Cmd c,uint16_t=0){if(c==Cmd::Stop){++stops;current={};}return true;}
inline Status status(){return current;}
inline bool seqIn=true;
inline bool seqInternal(){return seqIn;}
}
