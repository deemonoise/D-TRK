#include <FS.h>

#include <fstream>

#include "link/link.h"
#include "link/synth_fw.h"
#include "offline_render.h"
#include "storage/crashlog.h"
#include "synth.h"
namespace slink {
// Local filesystem and renderer do not require an external synth handshake.
bool synthUp() { return true; }
const char* synthFw() { return audio::synthFw(); }
uint32_t bootId() { return 1; }
bool request(mt::link::Msg t, const uint8_t* p, int n, mt::link::Msg replyType, uint8_t* reply, int& len,
             uint32_t) {
  if (t != replyType) return false;
  len = brick::renderRequest(t, p, n, reply);
  return len > 0;
}
bool requestLong(mt::link::Msg t, const uint8_t* p, int n, uint8_t* r, int& len, ProgressFn, void*,
                 uint32_t) {
  return request(t, p, n, t, r, len);
}
}  // namespace slink
namespace synthfw {
Result update(const char*, uint8_t& reason, uint32_t& bytes, ProgressFn, void*) {
  reason = uint8_t(mt::link::FwResult::BadImage);
  bytes = 0;
  return Result::Rejected;
}
const char* rejectText(uint8_t) { return "USE KNULLI PORTS"; }
bool rebooted(uint32_t) { return false; }
}  // namespace synthfw
namespace storage {
const char* firmwareRev() { return "D-TRK Brick"; }
const char* lastResetText() { return "APP START"; }
bool appendCpuProfile(const char* head, const audio::Profile& profile) {
  std::ofstream out(fs::resolve("/diag/cpuprof.txt"), std::ios::app);
  out << head << "Peak " << profile.peakUs << " us\n";
  for (int i = 0; i < audio::kProfStages; ++i)
    out << mt::Synth::profName(i) << " " << profile.us[i] << " us\n";
  return bool(out);
}
}  // namespace storage
