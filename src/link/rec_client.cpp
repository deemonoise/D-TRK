#include "rec_client.h"
#include "link.h"

using namespace mt::link;

namespace audio {

namespace {

constexpr uint32_t kShortMs = 150;

template <class Req>
bool shortReq(Msg t, const Req* q, uint8_t* reply, int& len) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  if (q) encode(*q, w);
  len = 0;
  return w.ok() && slink::request(t, p, w.size(), t, reply, len, kShortMs) && len > 0;
}

RecResult res(uint8_t r) {
  return r <= static_cast<uint8_t>(RecResult::CardFull) ? static_cast<RecResult>(r) : RecResult::BadArg;
}

void setLink(bool* link, bool v) {
  if (link) *link = v;
}

struct ProgCtx {
  BankProgressFn cb;
  void* ctx;
};

void onProgress(const Progress& p, void* c) {
  const ProgCtx* pc = static_cast<const ProgCtx*>(c);
  if (pc->cb) pc->cb(p.done, p.total, pc->ctx);
}

}  // namespace

RecResult recStart(const RecStartReq& q, uint16_t& number, bool* link) {
  uint8_t reply[kMaxPayload];
  int len = 0;
  RecStartRep rep;
  bool ok = shortReq(Msg::RecStart, &q, reply, len);
  if (ok) {
    Reader r(reply, len);
    ok = decode(r, rep);
  }
  setLink(link, ok);
  if (!ok) return RecResult::BadArg;
  if (rep.result == 0) number = rep.number;
  return res(rep.result);
}

RecResult recSet(int8_t gain, bool mon, uint8_t source, RecLevelRep* out, bool* link) {
  RecSetReq q;
  q.gain = gain;
  q.mon = mon ? 1 : 0;
  q.source = source;
  uint8_t reply[kMaxPayload];
  int len = 0;
  RecLevelRep rep;
  bool ok = shortReq(Msg::RecSet, &q, reply, len);
  if (ok) {
    Reader r(reply, len);
    ok = decode(r, rep);
  }
  setLink(link, ok);
  if (!ok) return RecResult::BadArg;
  if (out) *out = rep;
  return res(rep.result);
}

bool recLevel(RecLevelRep& rep) {
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!shortReq<RecSetReq>(Msg::RecLevel, nullptr, reply, len)) return false;
  Reader r(reply, len);
  return decode(r, rep);
}

RecResult recStop(RecStopRep& rep, BankProgressFn cb, void* ctx, bool* link) {
  uint8_t reply[kMaxPayload];
  int len = 0;
  ProgCtx pc{cb, ctx};
  bool ok = slink::requestLong(Msg::RecStop, nullptr, 0, reply, len, onProgress, &pc) && len > 0;
  bankViewStale();
  if (ok) {
    Reader r(reply, len);
    ok = decode(r, rep);
  }
  setLink(link, ok);
  if (!ok) return RecResult::BadArg;
  return res(rep.result);
}

RecResult recStartSd(uint8_t source, int8_t gain, int8_t thresh, bool mon, uint16_t& number, bool* link) {
  RecStartReq q;
  q.source = source;
  q.gain = gain;
  q.thresh = thresh;
  q.mon = mon ? 1 : 0;
  q.number = 1;  // the first free on the card: the project has no part in it
  q.toSd = 1;
  return recStart(q, number, link);
}

RecResult recCancel() {
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!shortReq<RecSetReq>(Msg::RecCancel, nullptr, reply, len)) return RecResult::BadArg;
  RecLevelRep rep;
  Reader r(reply, len);
  return decode(r, rep) ? res(rep.result) : RecResult::BadArg;
}

}  // namespace audio
