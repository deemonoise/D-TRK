"""Exercise the production directory filter with interrupted file rotations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StorageListingTest(unittest.TestCase):
    def test_backup_only_entries_remain_visible_without_mutating_directory(self):
        source = (ROOT / "src/hw/sdcard.cpp").read_text()
        helpers = source.split("int cmpName(", 1)[1].split("}  // namespace", 1)[0]
        listing = source.split("int sdList(", 1)[1].split("int sdListFiles(", 1)[0]
        fixture = r'''
#include <Arduino.h>
#include <cstdlib>
#include <strings.h>
#include "hw/sdcard.h"
#include "link_msg.h"
#include <cassert>
using String=std::string;
namespace storage {
inline int16_t error = 0;
int16_t remoteLastError() { return error; }
}
namespace hw {
int failAt = -1;
bool sdNextEntry(fs::File& dir,String& name,bool& isDir) {
 storage::error=0;
 if(failAt>=0 && dir.pos>=static_cast<size_t>(failAt)){storage::error=-4;return false;}
 std::vector<std::string> names;
 const std::string prefix=dir.path+"/";
 for(const auto& kv:fs::files)if(kv.first.find(prefix)==0)names.push_back(kv.first.substr(prefix.size()));
 if(dir.pos>=names.size())return false;name=names[dir.pos++];isDir=false;return true;
}
/* HELPERS */
/* LISTING */
}
int main() {
 fs::dirs.insert("/presets/CHIP");
 fs::files["/presets/CHIP/orphan.bak"]={1};
 fs::files["/presets/CHIP/both.bak"]={2};fs::files["/presets/CHIP/both.mti"]={3};
 fs::files["/presets/CHIP/work.tmp"]={4};
 char names[5][hw::kNameMax];const auto before=fs::files;
 assert(hw::sdList("/presets/CHIP",".mti",names,5,nullptr)==2);
 assert(std::string(names[0])=="both");assert(std::string(names[1])=="orphan");
 assert(fs::files==before);
 // A failed first page and a failed continuation must never look empty/complete.
 hw::failAt=0;assert(hw::sdList("/presets/CHIP",".mti",names,5,nullptr)==-1);
 hw::failAt=1;assert(hw::sdList("/presets/CHIP",".mti",names,5,nullptr)==-1);
 hw::failAt=-1;
 fs::failures.insert("open:/projects");
 assert(hw::sdList("/projects",".mtp",names,5,nullptr)==-1);
 fs::failures.clear();
 fs::dirs.insert("/projects");fs::files["/projects/song.bak"]={1};
 assert(hw::sdList("/projects",".mtp",names,5,nullptr)==1);assert(std::string(names[0])=="song");
 assert(hw::sdList("/projects",".wav",names,5,nullptr)==0);
 assert(hw::sdList("/projects",".mtp",names,5,[](const char*){return false;})==0);
}
'''
        fixture = fixture.replace("/* HELPERS */", "int cmpName(" + helpers)
        fixture = fixture.replace("/* LISTING */", "int sdList(" + listing)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "test.cpp").write_text(fixture)
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined",
                            "-I" + str(ROOT / "scripts/tests/storage_stubs"),
                            "-I" + str(ROOT / "lib/core/src"),
                            str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
            subprocess.run([str(folder / "test")], check=True)


    def test_failed_listing_keeps_counts_valid_and_discards_partial_entries(self):
        source = (ROOT / "src/ui/file_list.cpp").read_text()
        methods = source.split("void FileList::clear()", 1)[1].split("void FileList::finish(", 1)[0]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
using std::strncpy;
size_t strlcpy(char* out,const char* in,size_t cap) {const auto n=strlen(in);if(cap){strncpy(out,in,cap);out[cap-1]=0;}return n;}
namespace hw {
constexpr int kNameMax=96;
int dirs=2,files=3,single=2;
int sdListDirs(const char*,char (*)[kNameMax],int) {return dirs;}
int sdListFiles(const char*,const char* const*,int,char (*)[kNameMax],int) {return files;}
int sdList(const char*,const char*,char (*)[kNameMax],int,bool (*)(const char*)) {return single;}
}
class FileList {
public:
 enum class Kind {Dir,File,Action};
 struct Meta {Kind kind;uint8_t tag;int16_t index;};
 char data[8][hw::kNameMax]{};char (*names_)[hw::kNameMax]=data;
 Meta meta_[8]{};int cap_=8,count_=0,sel_=0,top_=0,dragAcc_=0;bool readError_=false;
 void previewStop() {}
 void clear();bool add(Kind,const char*,uint8_t,int16_t);
 void addFolder(const char*,const char* const*,int,bool=true,uint8_t=0,uint8_t=0);
 void addFiles(const char*,const char*,bool (*)(const char*),uint8_t=0);
};
/* METHODS */
int main() {
 FileList list;list.count_=2;hw::single=-1;
 list.addFiles("/projects",".mtp",nullptr);
 assert(list.count_==2);assert(list.readError_);
 list.clear();assert(!list.readError_);assert(list.count_==0);
 list.count_=1;hw::files=-1;
 list.addFolder("/presets",nullptr,1);
 assert(list.count_==1);assert(list.readError_);
 list.clear();list.count_=1;hw::dirs=-1;hw::files=3;
 list.addFolder("/presets",nullptr,1);
 assert(list.count_==1);assert(list.readError_);
 list.clear();hw::dirs=2;
 list.addFolder("/presets",nullptr,1);
 assert(list.count_==5);assert(!list.readError_);
 list.clear();hw::single=2;list.addFiles("/projects",".mtp",nullptr);
 assert(list.count_==2);assert(!list.readError_);
}
'''
        fixture = fixture.replace("/* METHODS */", "void FileList::clear()" + methods)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "test.cpp").write_text(fixture)
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined",
                            str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
            subprocess.run([str(folder / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
