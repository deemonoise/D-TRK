"""Compile the real UI-only DICE relocation against a bounded PSRAM allocator."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class DiceUndoRelocationTest(unittest.TestCase):
    def test_preserves_snapshot_and_allocation_failure(self):
        production=(ROOT/'src/ui/proj_screen.cpp').read_text()
        method='bool ProjScreen::relocateDiceUndo() {'+production.split('bool ProjScreen::relocateDiceUndo() {',1)[1].split('\n}\n',1)[0]+'\n}'
        source=r'''
#include <cassert>
#include <cstring>
#include <new>
#include <type_traits>
#include "dice.h"
#include "esp_heap_caps.h"
namespace ui {class ProjScreen {public:mt::DiceSnap* dice_=nullptr;bool relocateDiceUndo();};
/*METHOD*/
}
int main(){
 ui::ProjScreen screen;assert(screen.relocateDiceUndo());
 void* storage=heap_caps_malloc(sizeof(mt::DiceSnap),MALLOC_CAP_SPIRAM);
 screen.dice_=new(storage)mt::DiceSnap();
 // Exercise metadata plus the entire undo/work storage, rather than a shallow pointer copy.
 auto* bytes=reinterpret_cast<unsigned char*>(screen.dice_);
 for(size_t i=0;i<sizeof(mt::DiceSnap);++i)bytes[i]=static_cast<unsigned char>(i*37);
 unsigned char expected[sizeof(mt::DiceSnap)];memcpy(expected,bytes,sizeof expected);
 constexpr size_t padding=96*1024;
 const size_t held=sizeof(mt::DiceSnap);
 auto* before=screen.dice_;host_heap::psramBudget=host_heap::psramUsed;
 assert(!screen.relocateDiceUndo());assert(screen.dice_==before);
 assert(!memcmp(expected,screen.dice_,sizeof expected));
 assert(host_heap::psramUsed==held&&host_heap::psramBlocks.size()==1);
 assert(host_heap::rejected==1); // padding cannot be allocated
 host_heap::psramBudget=held+padding+held-1;
 assert(!screen.relocateDiceUndo());assert(screen.dice_==before);
 assert(!memcmp(expected,screen.dice_,sizeof expected));
 assert(host_heap::psramUsed==held&&host_heap::psramBlocks.size()==1);
 assert(host_heap::rejected==2); // padding fits, replacement does not; padding is returned
 host_heap::psramBudget=2*sizeof(mt::DiceSnap)+padding;
 assert(screen.relocateDiceUndo());assert(screen.dice_!=before);
 assert(!memcmp(expected,screen.dice_,sizeof expected));
 assert(host_heap::psramUsed==sizeof(mt::DiceSnap)&&host_heap::psramBlocks.size()==1);
 heap_caps_free(screen.dice_);
}
'''.replace('/*METHOD*/',method)
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory);(folder/'test.cpp').write_text(source)
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'scripts/tests/storage_stubs'),'-I'+str(ROOT/'lib/core/src'),str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
            subprocess.run([str(folder/'test')],check=True)
if __name__=='__main__':unittest.main()
