"""Compile the real hardware-free Dice dialog state and range policy."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class DiceDialogStateTest(unittest.TestCase):
    def test_forms_options_ranges_and_confirmation_keys(self):
        source = r'''
#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include "ui/dice_dialog_state.h"
using namespace ui::dice_dialog;
int main() {
 static_assert(static_cast<int>(mt::DiceForm::Classic)==0, "persisted Classic");
 static_assert(static_cast<int>(mt::DiceForm::Develop)==1, "persisted Develop");
 static_assert(static_cast<int>(mt::DiceForm::Arrange)==2, "append-only Arrange");
 static_assert(sizeof(Prefs)==20 && offsetof(Prefs,seed)==16, "stable edit-scope bytes");
 Prefs p;assert(p.form==1 && p.scope==kScAll);
 for(int form=0;form<3;++form){
  assert(clampForm(form)==form);
  assert(!strcmp(formName(form),form==0?"CLASSIC":form==1?"DEVELOP":"ARRANGE"));
 }
 assert(clampForm(3)==1 && clampForm(255)==1);
 assert(changeForm(0,-1)==0 && changeForm(0,1)==1);
 assert(changeForm(1,1)==2 && changeForm(2,1)==2 && changeForm(2,-1)==1);
 const int masks[]={mt::kDiceAll|mt::kDiceSong,mt::kDiceTempo,mt::kDiceSounds,mt::kDicePattern|mt::kDiceSong};
 for(int scope=0;scope<kScopes;++scope){p=Prefs();p.scope=scope;auto o=options(p);
  assert(o.layers==masks[scope]);assert(o.targetMask==63);assert(o.form==mt::DiceForm::Develop);
  assert(o.change==mt::DiceChange::Wild && o.seed==0 && o.root==-1);
  assert(o.groove==35 && o.density==mt::DiceDensity::Normal && o.variety==35);
  assert(o.len==0 && o.variant==mt::kDiceVariantAuto && o.clean && !o.lockMask);
 }
 p=Prefs();p.genre=3;p.style=2;p.target=3;p.change=0;p.form=2;p.feel=67;p.density=2;
 p.variant=15;p.clean=0;p.locks=17;p.fixedSeed=1;p.seed=345;p.len=3;p.song=0;p.variety=80;
 auto o=options(p);
 assert(o.genre==3 && o.style==2 && o.targetMask==4 && o.change==mt::DiceChange::Subtle);
 assert(o.form==mt::DiceForm::Arrange && o.groove==67 && o.density==mt::DiceDensity::Busy);
 assert(o.variant==15 && !o.clean && o.lockMask==17 && o.seed==345);
 assert(o.len==64 && o.layers==mt::kDiceAll && o.variety==80);
 p.scope=kScTempo;o=options(p);assert(o.targetMask==63 && o.change==mt::DiceChange::Wild);
 p=Prefs();p.form=2;o=options(p);auto six=range(o,15);
 assert(six.first==0 && six.count==6);
 for(int edit:{0,7,15})assert(range(o,edit)==six);
 char label[32];rangeTitle(label,sizeof(label),six,false);assert(!strcmp(label,"DICE P01-P06"));
 rangeTitle(label,sizeof(label),six,true);assert(!strcmp(label,"OVERWRITE P01-P06?"));
 o.form=mt::DiceForm::Develop;auto four=range(o,15);assert(four.first==0 && four.count==4);
 for(int edit:{0,7,15})assert(range(o,edit)==four);
 rangeTitle(label,sizeof(label),four,false);assert(!strcmp(label,"DICE P01-P04"));
 rangeTitle(label,sizeof(label),four,true);assert(!strcmp(label,"OVERWRITE P01-P04?"));
 o.form=mt::DiceForm::Classic;assert(range(o,15)==four);
 o.form=mt::DiceForm::Arrange;o.targetMask=4;assert(range(o,15)==four);
 o.targetMask=63;o.change=mt::DiceChange::Normal;assert(range(o,15)==four);
 o.change=mt::DiceChange::Wild;o.layers=mt::kDiceSounds;assert(range(o,15)==four);
 Range confirmed;assert((confirmed!=Range{0,4}));confirmed={0,4};
 assert((confirmed==Range{0,4}));assert((confirmed!=Range{0,6}));
 assert((confirmed!=Range{1,4}));
 mt::DiceOpts auditionOpts;
 assert(audition(auditionOpts,mt::kDicePattern,15,0)==1);
 assert(audition(auditionOpts,mt::kDicePattern,0,0)==1);
 auditionOpts.form=mt::DiceForm::Classic;
 assert(audition(auditionOpts,mt::kDicePattern,15,0)==0);
 auditionOpts.targetMask=2;
 assert(audition(auditionOpts,mt::kDicePattern,2,0)==-1);
 assert(audition(auditionOpts,mt::kDicePattern,15,0)==0);
 assert(audition(auditionOpts,mt::kDiceSounds,15,0)==-1);
 p=Prefs();assert(!options(p).keepKey);p.keepKey=1;assert(options(p).keepKey);
 rollSummary(label,sizeof(label),mt::kDiceSounds,9,2,123,false,false);assert(strstr(label,"SOUNDS ONLY") && !strstr(label,"NOTES"));
 rollSummary(label,sizeof(label),mt::kDiceAll,9,2,123,false,false);assert(strstr(label,"NEW ALL") && strstr(label,"Minor"));
 rollSummary(label,sizeof(label),mt::kDicePattern,9,2,123,true,false);assert(strstr(label,"FIXED NOTES"));
 rollSummary(label,sizeof(label),mt::kDicePattern,9,2,123,false,false,mt::DiceChange::Subtle);assert(strstr(label,"DYNAMICS ONLY"));
 LastRoll last;p=Prefs();assert(last.avoid(p)==255);last.remember(p,3);assert(last.avoid(p)==3);
 p.fixedSeed=1;assert(last.avoid(p)==255);p.fixedSeed=0;++p.style;assert(last.avoid(p)==255);

}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            (folder / "test.cpp").write_text(source)
            command = ["clang++", "-std=c++17", "-fsanitize=address,undefined",
                       "-ffunction-sections", "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                       "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/core/src"),
                       str(folder / "test.cpp"), str(ROOT / "lib/core/src/dice.cpp"), str(ROOT / "lib/core/src/scale.cpp"),
                       "-o", str(folder / "test")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(folder / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
