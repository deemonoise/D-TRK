"""Production touch tracker: raw drawing, capture and ordinary tap/drag regressions."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class TouchTrackerTest(unittest.TestCase):
    def test_raw_and_ordinary_gestures(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / 'hw').mkdir()
            (d / 'hw/lgfx_config.h').write_text('''#pragma once
#include <cstdint>
class LGFX { public: bool down=false; int x=0,y=0;
bool getTouch(int32_t* a,int32_t* b){*a=x;*b=y;return down;} };
''')
            (d / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
inline uint32_t nowMs=0;
inline uint32_t millis(){return nowMs;}
''')
            # Quoted includes resolve beside touch.cpp first; use actual sources copied unchanged.
            for name in ('touch.h', 'touch.cpp'):
                (d / name).write_text((ROOT / 'src/ui' / name).read_text())
            (d / 'theme.h').write_text('''#pragma once
namespace ui { constexpr int kScreenW=480,kScreenH=320,kAreaY=24,kTabY=296; }
''')
            (d / 'test.cpp').write_text(r'''
#include <cassert>
#include "Arduino.h"
#include "touch.h"
int main(){
 LGFX lcd; ui::TouchEvent e{};
 ui::TouchTracker raw;
 lcd.down=true;lcd.x=20;lcd.y=90;
 assert(raw.poll(lcd,e,true,true) && e.type==ui::TouchType::Down);
 auto id=e.id;
 lcd.x=22;lcd.y=92;
 assert(raw.poll(lcd,e,true,true) && e.type==ui::TouchType::Move && e.x==22 && e.y==92);
 // Remains raw even after leaving canvas/mode; never becomes a footer tap.
 lcd.x=470;lcd.y=310;
 assert(raw.poll(lcd,e,false,false) && e.type==ui::TouchType::Move && e.id==id);
 lcd.down=false;
 assert(raw.poll(lcd,e,false,false) && e.type==ui::TouchType::Up && e.y==310);
 assert(!raw.poll(lcd,e,false,false));
 ui::TouchTracker tap;
 lcd.down=true;lcd.x=12;lcd.y=10; // App status bar keeps ordinary tap in raw mode.
 assert(!tap.poll(lcd,e,true,true));lcd.down=false;
 assert(tap.poll(lcd,e,true,true) && e.type==ui::TouchType::Tap);
 ui::TouchTracker ordinary;
 lcd.down=true;lcd.x=100;lcd.y=100;
 assert(!ordinary.poll(lcd,e,false));
 lcd.y=110;assert(!ordinary.poll(lcd,e,false));
 lcd.y=120;assert(ordinary.poll(lcd,e,false) && e.type==ui::TouchType::Drag);
 lcd.down=false;assert(!ordinary.poll(lcd,e,false));
 ui::TouchTracker horizontal;
 lcd.down=true;lcd.x=100;lcd.y=100;assert(!horizontal.poll(lcd,e,true));
 lcd.x=115;assert(horizontal.poll(lcd,e,true) && e.type==ui::TouchType::HDrag);
 lcd.y=150;assert(!horizontal.poll(lcd,e,true)); // axis lock retained
 lcd.down=false;assert(!horizontal.poll(lcd,e,true));
}
''')
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I'+str(d),
                            str(d/'test.cpp'), str(d/'touch.cpp'), '-o', str(d/'test')], check=True)
            subprocess.run([str(d/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
