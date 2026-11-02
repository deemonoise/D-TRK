#pragma once
#include <cassert>
#include <cstdint>
namespace engine {
enum class Cmd {ReleaseTies,SetBpm,Stop};
inline int locks=0, lost=0, resets=0;
inline uint16_t tempo=0;
inline bool queueFull=false;
inline void lockProject() {++locks;}
inline void unlockProject() {--locks;}
inline bool savedBpm(uint16_t&) {return false;}
inline bool savedMutes(uint16_t&) {return false;}
inline bool post(Cmd c,uint16_t bpm=0) {if(queueFull){++lost;return false;} if(c==Cmd::SetBpm)tempo=bpm;return true;}
inline void projectReplaced(uint16_t bpm) {assert(locks>0);++resets;tempo=bpm;}
struct Status {bool playing=false,paused=false;};
inline Status status() {return {};}
}
