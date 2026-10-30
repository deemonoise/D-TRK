"""Bounded PSRAM: Dice and project I/O reuse one boot-reserved workspace."""
import os
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

class ProjectMemoryTest(unittest.TestCase):
    def test_dice_and_project_io_reuse_reserved_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / "esp_heap_caps.h").write_text(r'''
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
inline size_t available=600000;
inline int attempts=0;
inline void* heap_caps_malloc(size_t n,int){++attempts;if(n>available)return nullptr;void*p=malloc(n);if(p)available-=n;return p;}
''')
            (d / "test.cpp").write_text(r'''
#include <cassert>
#include <new>
#include "dice.h"
#include "project_memory.h"
#include "esp_heap_caps.h"
int main(){
 // Retry after failed boot reservation, without publishing a partial workspace.
 available=0;assert(!storage::reserveProjectMemory());
 available=600000;assert(storage::reserveProjectMemory());const int allocations=attempts;
 // All remaining heap becomes unavailable/fragmented after startup.
 available=0;
 void* base=nullptr;
 for(int i=0;i<50;++i){
  auto* draft=storage::allocateProjectMemory(sizeof(mt::DiceSnap));assert(draft);
  if(!base)base=draft;assert(base==draft);
  assert(!storage::allocateProjectMemory(sizeof(mt::Project))); // exclusive lease
  storage::releaseProjectMemory(draft);
  auto* saved=storage::allocateProjectMemory(sizeof(mt::Project));assert(saved==base);
  auto* bytes=static_cast<unsigned char*>(saved);
  bytes[0]='T';bytes[sizeof(mt::Project)-1]=123;
  assert(bytes[0]=='T' && bytes[sizeof(mt::Project)-1]==123);
  storage::releaseProjectMemory(saved);
 }
 assert(attempts==allocations);
 assert(!storage::allocateProjectMemory(sizeof(mt::Project)+1));
 assert(storage::allocateProjectMemory(sizeof(mt::Project))==base);
 storage::releaseProjectMemory(reinterpret_cast<void*>(1)); // wrong owner cannot release
 assert(!storage::allocateProjectMemory(1));
 storage::releaseProjectMemory(base);assert(storage::allocateProjectMemory(1)==base);
 storage::releaseProjectMemory(base);
}
''')
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-I"+str(d), "-I"+str(ROOT/"lib/core/src"), "-I"+str(ROOT/"src/storage"), str(d/"test.cpp"), str(ROOT/"src/storage/project_memory.cpp"), "-o", str(d/"test")], check=True)
            subprocess.run([str(d/"test")], check=True)

    @unittest.skipUnless(os.environ.get("DTRK_NATIVE_CORE"), "build native core and set DTRK_NATIVE_CORE")
    def test_real_dice_save_load_after_heap_exhaustion(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d/"esp_heap_caps.h").write_text(r'''#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
inline size_t available=600000;
inline void* heap_caps_malloc(size_t n,int){if(n>available)return nullptr;void*p=malloc(n);if(p)available-=n;return p;}
''')
            (d/"test.cpp").write_text(r'''#include <cassert>
#include <cstring>
#include <new>
#include <vector>
#include "dice.h"
#include "project_io.h"
#include "project_memory.h"
#include "esp_heap_caps.h"
struct Sink:mt::ByteSink{std::vector<unsigned char> bytes;bool write(const void*d,size_t n)override{auto*p=(const unsigned char*)d;bytes.insert(bytes.end(),p,p+n);return true;}};
struct Source:mt::ByteSource{const std::vector<unsigned char>& bytes;size_t at=0;Source(const std::vector<unsigned char>&b):bytes(b){}bool read(void*d,size_t n)override{if(n>bytes.size()-at)return false;memcpy(d,bytes.data()+at,n);at+=n;return true;}bool skip(size_t n)override{if(n>bytes.size()-at)return false;at+=n;return true;}};
static mt::Project live;
int main(){
 assert(storage::reserveProjectMemory());available=0;
 for(int i=0;i<36;++i){
  void* memory=storage::allocateProjectMemory(sizeof(mt::DiceSnap));assert(memory);
  auto* draft=new(memory)mt::DiceSnap();mt::DiceOpts opts;opts.genre=i%9;
  opts.layers=mt::kDiceAll|mt::kDiceSong;auto result=mt::diceRoll(live,opts,0,123+i,*draft);
  assert(result.written);draft->~DiceSnap();storage::releaseProjectMemory(memory);
  memory=storage::allocateProjectMemory(sizeof(mt::Project));assert(memory);
  auto* snapshot=new(memory)mt::Project(live);Sink saved;assert(mt::saveProject(*snapshot,saved));
  snapshot->~Project();storage::releaseProjectMemory(memory);
  memory=storage::allocateProjectMemory(sizeof(mt::Project));assert(memory);
  auto* loaded=new(memory)mt::Project();Source input(saved.bytes);
  assert(mt::loadProject(input,*loaded)==mt::LoadErr::Ok);Sink checked;assert(mt::saveProject(*loaded,checked));
  assert(saved.bytes==checked.bytes);live=*loaded;loaded->~Project();storage::releaseProjectMemory(memory);
 }
}
''')
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-I"+str(d), "-I"+str(ROOT/"lib/core/src"), "-I"+str(ROOT/"src/storage"), str(d/"test.cpp"), str(ROOT/"src/storage/project_memory.cpp"), os.environ["DTRK_NATIVE_CORE"], "-o", str(d/"test")], check=True)
            subprocess.run([str(d/"test")], check=True)

if __name__ == "__main__":
    unittest.main()
