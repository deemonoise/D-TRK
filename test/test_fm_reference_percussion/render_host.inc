#include "synth_fm.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
// Standalone host driver: shared sine table uses the production initializer formula.
namespace mt { float gSine[kSineLen + 1]; }
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  for (int k = 0; k <= mt::kSineLen; ++k) mt::gSine[k] = std::sin(6.28318530718f*k/mt::kSineLen);
  std::ifstream rows(argv[1]); std::string line;
  while (std::getline(rows, line)) {
    std::istringstream in(line); int machine, vel; float note, seconds, raw[8]; std::string path;
    in >> machine >> note >> vel >> seconds;
    for (float& p : raw) in >> p;
    in >> path; if (!in) return 3;
    mt::FmParams p; if (!mt::fmPercussionMachine(machine, raw, note, p)) return 4;
    std::vector<float> audio(static_cast<size_t>(seconds*mt::kSynthRate));
    mt::FmVoice voice; voice.trigger(false);
    for (size_t off=0; off<audio.size(); off+=64) {
      const int n=static_cast<int>(std::min(size_t(64),audio.size()-off));
      voice.control(p,n); voice.render(audio.data()+off,n,vel/127.f);
    }
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(audio.data()),audio.size()*sizeof(float));
  }
}
