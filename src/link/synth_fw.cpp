#include "synth_fw.h"
#include <string.h>
#include "link.h"
#include "link_msg.h"

using namespace mt::link;

namespace synthfw {

namespace {

struct Ctx {
  ProgressFn cb;
  void* ctx;
};

void onProgress(const Progress& p, void* c) {
  const Ctx* k = static_cast<const Ctx*>(c);
  if (k->cb) k->cb(p.done, p.total, k->ctx);
}

}  // namespace

Result update(const char* path, uint8_t& fwResult, uint32_t& bytes, ProgressFn cb, void* ctx) {
  fwResult = 0;
  bytes = 0;
  FwFromFileReq q;
  if (!path || strlen(path) >= sizeof q.path) {
    fwResult = static_cast<uint8_t>(FwResult::OpenFail);
    return Result::Rejected;
  }
  strcpy(q.path, path);
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(q, w);
  uint8_t reply[kMaxPayload];
  int len = 0;
  Ctx k{cb, ctx};
  // Erasing the staging area alone takes ~10 s: Progress frames keep the request alive.
  if (!w.ok() || !slink::requestLong(Msg::FwFromFile, p, w.size(), reply, len, onProgress, &k, 5000))
    return Result::NoSynth;
  FwRep rep;
  Reader r(reply, len);
  if (!decode(r, rep)) return Result::NoSynth;
  fwResult = rep.result;
  bytes = rep.bytes;
  return rep.result == static_cast<uint8_t>(FwResult::Ok) ? Result::Ok : Result::Rejected;
}

const char* rejectText(uint8_t r) {
  switch (static_cast<FwResult>(r)) {
    case FwResult::Ok: return "OK";
    case FwResult::NoSd: return "NO CARD";
    case FwResult::OpenFail: return "NO FILE";
    case FwResult::ReadFail: return "READ FAILED";
    case FwResult::BadHex: return "BAD HEX FILE";
    case FwResult::TooBig: return "IMAGE TOO BIG";
    case FwResult::BadImage: return "NOT A SYNTH FIRMWARE";
    case FwResult::FlashFail: return "FLASH WRITE FAILED";
    case FwResult::Busy: return "SYNTH BUSY";
  }
  return "?";
}

bool rebooted(uint32_t oldBoot) {
  const uint32_t b = slink::bootId();
  return b != 0 && b != oldBoot && slink::synthUp();
}

}  // namespace synthfw
