#pragma once
#include "link_fs.h"

namespace mt::link {

// A single in-flight filesystem request. Receipt and expiry do no card I/O; step() is called
// only after the stream scheduler has admitted background work. The wire already serializes
// FsClient requests. An overlapping request fails explicitly rather than replacing the first.
class FsDeferred {
 public:
  using Reply = void (*)(void* ctx, Msg type, uint8_t seq, const uint8_t* payload, int size);
  // Below the ESP FsClient's 2000-ms first retry; a retry never restarts this deadline.
  static constexpr uint32_t kWaitMs = 1000;
  FsDeferred(FsServerCore& server, Reply reply, void* ctx) : server_(server), reply_(reply), ctx_(ctx) {}

  // -1: queued / pending retry, or not an Fs request; >=0: immediate reply size in out.
  // Retries match payload bytes as well as type+seq: the wire counter also advances for Time/Ev.
  int submit(Msg type, uint8_t seq, const uint8_t* payload, int size, uint32_t now, uint8_t* out);
  // One admitted portion: deferred handle cleanup, or a request (never both in one call).
  // idle must exclude active voices, stream owners, queued audio events and live transport:
  // cleanup and known monolithic operations require this stronger audio-idle snapshot.
  // A short request bounds the payload, not an arbitrary card's latency.
  bool step(uint32_t now, bool idle);
  // Called from every loop pass even if background work is starved. Sends a cached kErrIo
  // without executing an expired request. This is an operation failure, never an empty success.
  bool expire(uint32_t now);
  // A link reset cancels the pending request and clears replies without card I/O. The next
  // admitted idle step closes the old handles before any subsequent request can execute.
  void reset();
  bool waiting() const { return pending_ || needsReset_; }

 private:
  void finish(int size);
  FsServerCore& server_;
  Reply reply_;
  void* ctx_;
  bool pending_ = false, cached_ = false, needsReset_ = false;
  Msg type_ = Msg::Hello, cachedType_ = Msg::Hello;
  uint8_t seq_ = 0, cachedSeq_ = 0;
  // This wrapper owns wire retries. The inner synchronous server must see a fresh execution
  // id even after 256 wire requests expired without running, otherwise its old cache can match.
  uint8_t executionSeq_ = 0;
  uint32_t queuedAt_ = 0;
  int size_ = 0, replySize_ = 0, cachedSize_ = 0;
  uint8_t payload_[kMaxPayload];
  uint8_t cachedPayload_[kMaxPayload];
  uint8_t answer_[kMaxPayload];
};

}  // namespace mt::link
