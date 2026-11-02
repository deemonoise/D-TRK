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
#include <cassert>
using String=std::string;
namespace hw {
bool sdNextEntry(fs::File& dir,String& name,bool& isDir) {
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


if __name__ == "__main__":
    unittest.main()
