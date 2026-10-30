"""Compile the real SD loader against a deterministic in-memory card."""
import pathlib
import os
import sys
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class DiceLibraryIoTest(unittest.TestCase):
    def test_sd_fallback_short_reads_limits_and_releases(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / "hw").mkdir()
            (d / "hw/sdcard.h").write_text(r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace hw {
inline bool ready = true;
inline std::map<std::string,std::vector<uint8_t>> files;
inline std::string opened;
inline bool shortRead = false;
inline size_t bytesRead = 0;
struct File {
 std::vector<uint8_t>* bytes = nullptr;
 size_t pos=0;
 explicit operator bool() const {return bytes;}
 bool isDirectory() const {return false;}
 size_t size() const {return bytes->size();}
 size_t read(uint8_t* p,size_t n) {
  if(shortRead && pos>0)return 0;
  n=std::min(n,bytes->size()-pos);
  memcpy(p,bytes->data()+pos,n);pos+=n;bytesRead+=n;return n;
 }
 bool seek(uint32_t at){if(at>bytes->size())return false;pos=at;return true;}
 void close(){}
};
struct Fs {
 File open(const char* path,const char*) {
  opened=path;auto it=files.find(path);
  return it==files.end()?File{}:File{&it->second};
 }
};
inline bool sdReady(){return ready;}
inline Fs& sdFs(){static Fs fs;return fs;}
}
namespace fs {using File=hw::File;}
''')
            (d / "esp_heap_caps.h").write_text(r'''
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
inline int allocations=0;
inline size_t allocationLimit=32768,largestAllocation=0;
inline void* heap_caps_malloc(size_t n,int){largestAllocation=n>largestAllocation?n:largestAllocation;if(n>allocationLimit)return nullptr;void*p=malloc(n);if(p)++allocations;return p;}
inline void heap_caps_free(void*p){if(p)--allocations;free(p);}
''')
            (d / "test.cpp").write_text(r'''
#include <cassert>
#include <cstdio>
#include <fstream>
#include "storage/dice_library_io.h"
#include "hw/sdcard.h"
#include "esp_heap_caps.h"
static void put16(std::vector<uint8_t>& b,size_t at,int v){b[at]=v;b[at+1]=v>>8;}
static void put32(std::vector<uint8_t>& b,size_t at,uint32_t v){put16(b,at,v);put16(b,at+2,v>>16);}
static std::vector<uint8_t> makeLibrary(){
 constexpr int count=1996,table=16+64*count;std::vector<uint8_t>b(735568);
 memcpy(b.data(),"DML1",4);put16(b,4,1);put16(b,6,64);put32(b,8,count);
 for(int i=0;i<count;++i){size_t at=16+i*64;put16(b,at,1);b[at+2]=i%6;b[at+4]=2;b[at+5]=1;
 put32(b,at+8,table+i*8);put32(b,at+12,8);snprintf((char*)b.data()+at+16,48,"phrase %d",i);
 put16(b,table+i*8,0);put16(b,table+i*8+2,24);b[table+i*8+4]=i%128;b[table+i*8+5]=100;b[table+i*8+6]=255;}
 return b;
}
int main(int argc,char** argv){
 {
 storage::DiceLibraryData data;
 assert(!data && !data.size());assert(!data.load());assert(allocations==0);
 auto binary=makeLibrary();hw::files["/samples/midi/dice-library.dml"]=binary;
 assert(data.load(0,777) && data.size()==735568);
 assert(hw::opened=="/samples/midi/dice-library.dml");
 assert(allocations==1 && largestAllocation<=32768);
 assert(hw::bytesRead<150000); // only directory and selected phrases, not 735KB
 mt::DiceMidiLibrary full(binary.data(),binary.size());assert(full.valid());
 for(int k=0;k<6;++k){mt::DicePhrase a,b;uint32_t seed=777^(0x9E3779B9u*(k+1));
 assert(data.pick(0,(mt::DiceRole)k,seed,a));assert(full.pick(0,(mt::DiceRole)k,seed,b));
 assert(a.count==b.count && !strcmp(a.name,b.name) && a.events[0].note==b.events[0].note);
 assert(!data.pick(0,(mt::DiceRole)k,seed+1,a));}
 hw::shortRead=true;assert(!data.load() && !data && allocations==0);hw::shortRead=false;
 hw::files["/midi/dice-library.dml"]=std::vector<uint8_t>(storage::DiceLibraryData::kMaxBytes+1,42);
 assert(!data.load() && !data && allocations==0);
 hw::files["/midi/dice-library.dml"]=std::vector<uint8_t>(4,42);assert(!data.load() && allocations==0);
 hw::files["/midi/dice-library.dml"]=std::vector<uint8_t>(16,24);assert(!data.load() && allocations==0);
 hw::files["/midi/dice-library.dml"]=binary;
 hw::ready=false;assert(!data.load() && allocations==0);hw::ready=true;
 allocationLimit=0;assert(!data.load() && allocations==0);allocationLimit=32768;
 assert(data.load() && allocations==1);
 mt::DicePhrase ignored;assert(!data.pick(1,mt::DiceRole::Bass,1,ignored)); // wrong genre
 }
 assert(allocations==0);
 if(argc>1){
  std::ifstream in(argv[1],std::ios::binary);
  std::vector<uint8_t> corpus((std::istreambuf_iterator<char>(in)),{});
  assert(!corpus.empty());mt::DiceMidiLibrary reference(corpus.data(),corpus.size());assert(reference.valid());
  hw::files["/midi/dice-library.dml"]=corpus;storage::DiceLibraryData streamed;
  for(int genre=0;genre<9;++genre)for(uint32_t seed=1;seed<=12;++seed){
   assert(streamed.load(genre,seed));
   for(int role=0;role<6;++role){mt::DicePhrase a,b;uint32_t rs=seed^(0x9E3779B9u*(role+1));
    bool expected=reference.pick(genre,(mt::DiceRole)role,rs,a);
    assert(expected==streamed.pick(genre,(mt::DiceRole)role,rs,b));
    if(expected)assert(a.count==b.count && !strcmp(a.name,b.name) &&
        !memcmp(a.events,b.events,a.count*sizeof(mt::DicePhraseEvent)));
   }
  }
 }
}
''')
            cmd = ["clang++", "-std=c++17", "-fsanitize=address,undefined", "-ffunction-sections", "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                   "-I" + str(d), "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                   str(ROOT / "src/storage/dice_library_io.cpp"), str(ROOT / "lib/core/src/dice_library.cpp"),
                   str(d / "test.cpp"),
                   "-o", str(d / "test")]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            args = [str(d / "test")]
            if os.environ.get("DICE_LIBRARY_FIXTURE"):
                args.append(os.environ["DICE_LIBRARY_FIXTURE"])
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
