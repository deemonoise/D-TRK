"""Check the production replacement mailbox and queue-drain order without FreeRTOS."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EngineReplacementTest(unittest.TestCase):
    def test_full_queue_and_latest_replacement_apply_before_planning(self):
        source = (ROOT / "src/engine/engine.cpp").read_text()
        notify = source.split("void projectReplaced(", 1)[1].split("Status status()", 1)[0]
        drain = source.split("    Command c;", 1)[1].split("    for (uint32_t b = mutClearBits", 1)[0]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include "practice.h"
mt::RecordPractice practice;
enum class Cmd {SetBpm,ReleaseTies,Stop};
struct Command {Cmd cmd;uint8_t arg2;uint16_t arg;};
std::atomic<uint32_t> replacementBpm{0};
int task=1,cmds=1,notifications=0,releases=0,stops=0;
uint16_t tempo=0;
std::deque<Command> queue;
std::vector<Cmd> handled;
constexpr int pdTRUE=1;
void xTaskNotifyGive(int t) {assert(t==task);++notifications;}
int xQueueReceive(int q,Command* c,int wait) {
 assert(q==cmds&&wait==0);if(queue.empty())return 0;
 *c=queue.front();queue.pop_front();return pdTRUE;
}
void handle(const Command& c) {
 handled.push_back(c.cmd);if(c.cmd==Cmd::SetBpm)tempo=c.arg;else if(c.cmd==Cmd::Stop){practice.stop();++stops;}else ++releases;
}
/* NOTIFY */
void drainBeforePlanning() {Command c; /* DRAIN */ }
int main() {
 for(int i=0;i<128;++i)queue.push_back({Cmd::SetBpm,0,80});
 practice.arm(true,false,0,0);practice.startCount(0,120,1);assert(practice.counting());
 projectReplaced(120);assert(queue.size()==128);assert(releases==0);assert(notifications==1);
 drainBeforePlanning();assert(stops==1);assert(!practice.counting());assert(queue.empty());assert(tempo==120);assert(releases==1);
 assert(handled[handled.size()-3]==Cmd::Stop);assert(handled[handled.size()-2]==Cmd::ReleaseTies);assert(handled.back()==Cmd::SetBpm);
 const auto count=handled.size();drainBeforePlanning();assert(handled.size()==count);
 projectReplaced(160);projectReplaced(220);drainBeforePlanning();
 assert(tempo==220);assert(releases==2);assert(notifications==3);
}
'''
        fixture = fixture.replace("/* NOTIFY */", "void projectReplaced(" + notify)
        fixture = fixture.replace("/* DRAIN */", drain)
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "test.cpp").write_text(fixture)
            subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I" + str(ROOT / "lib/core/src"),
                            str(ROOT / "lib/core/src/practice.cpp"), str(folder / "test.cpp"),
                            "-o", str(folder / "test")], check=True)
            subprocess.run([str(folder / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
