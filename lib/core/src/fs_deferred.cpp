#include "fs_deferred.h"
#include <string.h>
#include "hot.h"

namespace mt::link {
namespace {
int errorReply(int16_t error, uint8_t* out) {
  out[0] = static_cast<uint8_t>(error);
  out[1] = static_cast<uint8_t>(static_cast<uint16_t>(error) >> 8);
  return 2;
}

bool needsIdle(const FsServerCore& server, Msg type, const uint8_t* payload, int size) {
  switch (type) {
    case Msg::FsOpen:
    case Msg::FsRead:
    case Msg::FsWrite:
    case Msg::FsClose: return false;
    // Metadata writes can allocate clusters or sync directories; reserve is not a latency bound.
    case Msg::FsMkdir:
    case Msg::FsRename: return true;
    case Msg::FsList: {
      Reader r(payload, size);
      const uint16_t start = r.u16();
      r.u8();  // max entries; only the cursor determines whether this is a bounded page
      char dir[kFsPathMax];
      r.str(dir, sizeof dir);
      // Let the server reject malformed requests rather than waiting for audio to stop.
      if (!r.ok() || dir[0] != '/') return false;
      return !server.listCanContinue(dir, start);
    }
    case Msg::FsStat: {
      // path length + path [, flags]; malformed paths are still rejected by FsServerCore.
      if (!size) return false;
      const int flags = 1 + payload[0];
      return flags < size && (payload[flags] & kFsStatSpace);
    }
    default: return true;
  }
}
}  // namespace

MT_CONTROL int FsDeferred::submit(Msg type, uint8_t seq, const uint8_t* payload, int size,
                                  uint32_t now, uint8_t* out) {
  if (!isFsRequest(type)) return -1;
  if (size < 0 || size > kMaxPayload || (!payload && size)) return errorReply(kErrBadArg, out);
  if (pending_ && type == type_ && seq == seq_ && size == size_ &&
      (!size || memcmp(payload, payload_, size) == 0)) return -1;
  if (cached_ && type == cachedType_ && seq == cachedSeq_ && size == cachedSize_ &&
      (!size || memcmp(payload, cachedPayload_, size) == 0)) {
    memcpy(out, answer_, replySize_);
    return replySize_;
  }
  if (pending_) return errorReply(kErrIo, out);
  type_ = type;
  seq_ = seq;
  size_ = size;
  queuedAt_ = now;
  if (size) memcpy(payload_, payload, size);
  pending_ = true;
  return -1;
}

MT_CONTROL void FsDeferred::finish(int size) {
  replySize_ = size;
  cachedType_ = type_;
  cachedSeq_ = seq_;
  cachedSize_ = size_;
  if (size_) memcpy(cachedPayload_, payload_, size_);
  cached_ = true;
  pending_ = false;
  reply_(ctx_, type_, seq_, answer_, size);
}

MT_CONTROL bool FsDeferred::expire(uint32_t now) {
  if (!pending_ || now - queuedAt_ < kWaitMs) return false;
  finish(errorReply(kErrIo, answer_));
  return true;
}

MT_CONTROL bool FsDeferred::step(uint32_t now, bool idle) {
  if (expire(now)) return true;
  if (needsReset_) {
    if (!idle) return false;
    server_.reset();
    needsReset_ = false;
    executionSeq_ = 0;
    return true;
  }
  if (!pending_) return false;
  if (!idle && needsIdle(server_, type_, payload_, size_)) return false;
  finish(server_.handle(type_, executionSeq_++, payload_, size_, answer_));
  return true;
}

MT_CONTROL void FsDeferred::reset() {
  pending_ = cached_ = false;
  needsReset_ = true;
}

}  // namespace mt::link
