"""Run FILE page switching with production navigation methods and ParamList."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def block(source, signature):
    start = source.index(signature)
    depth = 0
    for end in range(source.index("{", start), len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise AssertionError("unterminated block: " + signature)


class FilePagesTest(unittest.TestCase):
    def test_page_chords_preserve_row_or_bar_focus(self):
        source = (ROOT / "src/ui/file_screen.cpp").read_text()
        header = (ROOT / "src/ui/file_screen.h").read_text()
        methods = ["void FileScreen::onPage(", "void FileScreen::setSection(",
                   "void FileScreen::showSystem(", "bool FileScreen::enabled(",
                   "void FileScreen::moveSel("]
        if "bool FileScreen::sectionBarSelected(" in source:
            methods.append("bool FileScreen::sectionBarSelected(")
        declarations = [re.search(r"\b(?:void|bool) " + name + r"\([^;{]*;", header)[0]
                        .replace(" override", "") for name in
                        ("onPage", "setSection", "showSystem", "enabled", "moveSel")]
        if len(methods) == 6:
            declarations.append("bool sectionBarSelected() const;")
        enums = "\n".join(block(header, "enum " + name) + ";" for name in
                          ("Action", "Section", "SampleRow", "SysRow"))
        fixture = r'''
#include <cassert>
#include "ui/param_list.h"
#include "ab_keys.h"
namespace hw { inline bool card=true; bool sdReady(){return card;} }
namespace ui {
struct App { int redraws=0; void invalidate(){++redraws;} };
struct Overlay { bool open=false; int pages=0; bool isOpen()const{return open;} void onPage(int d){pages+=d;} };
struct Browser { bool open=false; bool ready()const{return open;} };
class FileScreen {
 public:
 /* ENUMS */
 static constexpr int kSectionSel=kActions,kSysListRows=8;
 App app_; Overlay kb_,import_,render_,wifi_,midiDlg_; Browser list_;
 int section_=kSecProjects,sel_=kSave,ssel_=kImportRow,stop_=0,systemSel_=kDiagnostics;
 bool diagnostics_=false,autoAvail_=false;
 Param sysParams_[kSysRows]{}; ParamList sysList_;
 FileScreen(){sysList_.setPageBar(true);sysList_.setParams(sysParams_,kFirmware);}
 /* DECLARATIONS */
 bool bar()const {
  return section_==kSecProjects ? sel_==kSectionSel : section_==kSecSamples ? ssel_==kSwitchRow : sysList_.barSelected();
 }
 void row(){if(section_==kSecProjects)sel_=kLoad;else if(section_==kSecSamples)ssel_=kFirstSample;else{sysList_.setSel(3);sysList_.setEdit(true);}}
 void chord(int delta,bool shifted=false){
  mt::AbKeys keys;keys.bDown();if(!shifted)keys.aDown();
  auto out=keys.turn(delta,shifted);assert(out.act==mt::AbAction::PageTurn);onPage(out.delta);
  assert(keys.bUp(shifted).act==mt::AbAction::None);
  if(!shifted)assert(keys.aUp(false).act==mt::AbAction::None);
 }
};
/* METHODS */
}
int main(){
 using ui::FileScreen;
 for(bool shifted:{false,true})for(int from=0;from<3;++from)for(int delta:{-4,-1,1,4}) {
  FileScreen f;f.setSection(from);f.row();f.chord(delta,shifted);
  assert(f.section_==((from+delta)%3+3)%3);
  assert(!f.bar());assert(!f.sysList_.editing());
  if(f.section_==FileScreen::kSecProjects)assert(f.sel_==FileScreen::kSave);
  else if(f.section_==FileScreen::kSecSamples)assert(f.ssel_==FileScreen::kImportRow && f.stop_==0);
  else assert(f.sysList_.sel()==0);
  // A bar selection intentionally remains a bar selection, including wraparound.
  f.setSection(from);assert(f.bar());f.chord(delta,shifted);assert(f.bar());
 }
 // Without a card, the shortcut still lands on an enabled action, not on the bar.
 hw::card=false;FileScreen noCard;noCard.setSection(FileScreen::kSecSamples);noCard.row();noCard.chord(-1);
 assert(noCard.sel_==FileScreen::kRetry && !noCard.bar());hw::card=true;
 // Page gestures must not leave a modal; MIDI owns its internal pages.
 for(int overlay=0;overlay<6;++overlay){
  FileScreen f;f.row();
  if(overlay==0)f.kb_.open=true;else if(overlay==1)f.import_.open=true;
  else if(overlay==2)f.render_.open=true;else if(overlay==3)f.wifi_.open=true;
  else if(overlay==4)f.list_.open=true;else f.midiDlg_.open=true;
  f.chord(1);assert(f.section_==FileScreen::kSecProjects && f.sel_==FileScreen::kLoad);
  assert(f.midiDlg_.pages==(overlay==5 ? 1 : 0));
 }
 // Leaving diagnostics through the shortcut also keeps focus in the content.
 FileScreen diag;diag.setSection(FileScreen::kSecSystem);diag.showSystem(true);diag.chord(1);
 assert(diag.section_==FileScreen::kSecProjects && !diag.bar());
}
'''.replace("/* ENUMS */", enums).replace("/* DECLARATIONS */", "\n".join(declarations))
        fixture = fixture.replace("/* METHODS */", "\n".join(block(source, name) for name in methods))
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            for sub in ("ui", "hw", "engine"):
                (folder / sub).mkdir()
            for name in ("param_list.h", "param_list.cpp", "touch.h"):
                (folder / "ui" / name).write_text((ROOT / "src/ui" / name).read_text())
            (folder / "hw/input.h").write_text((ROOT / "src/hw/input.h").read_text())
            (folder / "Arduino.h").write_text("#pragma once\n#include <cstdint>\nusing TickType_t=uint32_t;\n")
            (folder / "hw/lgfx_config.h").write_text("""#pragma once
class LGFX {};
struct LGFX_Sprite { void fillRect(int,int,int,int,int){} void setTextColor(int){}
void drawString(const char*,int,int){} };
""")
            (folder / "ui/theme.h").write_text("""#pragma once
namespace ui { constexpr int kAreaY=24,kScreenW=480,kCharH=16,kRowH=24,
kSelBg=1,kCursor=2,kDim=3,kText=4,kEditCursor=5,kRed=6; }
""")
            (folder / "engine/engine.h").write_text("#pragma once\nnamespace engine { inline void lockProject(){} inline void unlockProject(){} }\n")
            (folder / "esp_heap_caps.h").write_text("#pragma once\n#include <cstdlib>\n#define MALLOC_CAP_SPIRAM 0\ninline void* heap_caps_malloc(size_t n,int){return malloc(n);}\n")
            (folder / "test.cpp").write_text(fixture)
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I" + str(folder),
                            "-I" + str(ROOT / "lib/core/src"), str(folder / "test.cpp"),
                            str(folder / "ui/param_list.cpp"), "-o", str(folder / "test")], check=True)
            result = subprocess.run([str(folder / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
