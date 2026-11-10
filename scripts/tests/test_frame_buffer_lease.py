"""Exercise the production framebuffer lease with a failing fake sprite allocator."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class FrameBufferLeaseTest(unittest.TestCase):
    def test_release_restore_nested_and_failed_restore_retry(self):
        source=r'''
#include <cassert>
#include "ui/frame_buffer_lease.h"
struct Sprite {bool allocated=true;int frees=0;void deleteSprite(){allocated=false;++frees;}};
struct Context {int waits=0,creates=0;bool fail=false;};
void wait(void* p){++static_cast<Context*>(p)->waits;}
bool create(Sprite& s,void* p){auto& c=*static_cast<Context*>(p);++c.creates;s.allocated=!c.fail;return s.allocated;}
int main(){
 Sprite sprite;Sprite* visible=&sprite;ui::FrameBufferState<Sprite> state;Context c;
 {
  ui::FrameBufferLease<Sprite> outer(state,visible,wait,create,&c);
  assert(state.loaned&&!visible&&!sprite.allocated&&c.waits==1);
  assert(!ui::restoreFrameBuffer(state,visible,create,&c));assert(c.creates==0);
  {ui::FrameBufferLease<Sprite> nested(state,visible,wait,create,&c);assert(state.loaned&&!visible);}
  assert(state.loaned&&c.creates==0&&c.waits==1);
 }
 assert(visible==&sprite&&sprite.allocated&&!state.loaned&&!state.retained&&c.creates==1);
 c.fail=true;
 {ui::FrameBufferLease<Sprite> loan(state,visible,wait,create,&c);}
 assert(!visible&&!sprite.allocated&&state.retained==&sprite&&!state.loaned);
 assert(!ui::restoreFrameBuffer(state,visible,create,&c));assert(!visible);
 c.fail=false;assert(ui::restoreFrameBuffer(state,visible,create,&c));assert(visible==&sprite&&sprite.allocated);
 const int made=c.creates;assert(ui::restoreFrameBuffer(state,visible,create,&c));assert(c.creates==made);
 Sprite* absent=nullptr;ui::FrameBufferState<Sprite> empty;Context e;
 {ui::FrameBufferLease<Sprite> loan(empty,absent,wait,create,&e);assert(empty.loaned&&!absent);}
 assert(!empty.loaned&&!absent&&!empty.retained&&e.waits==0&&e.creates==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory);(folder/'test.cpp').write_text(source)
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'src'),str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
            subprocess.run([str(folder/'test')],check=True)
if __name__=='__main__':unittest.main()
