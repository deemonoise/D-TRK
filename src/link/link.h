#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "link_msg.h"

// ESP end of the link to the synth board. One FreeRTOS task owns the UART: it parses replies and
// sends, events (engine) first, then Time every 50 ms, then one queued bulk frame at a time.
namespace slink {  // not "link": POSIX link() (unistd.h) takes that name

void begin();

// Engine task only (single producer): an INT event stamped with its engine::nowUs() time.
void postEvent(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len);

// UI task: queues a whole frame of type t; false when the queue stays full for wait.
bool sendRaw(mt::link::Msg t, const uint8_t* payload, int n, TickType_t wait = 0);
template <class M>
bool send(mt::link::Msg t, const M& m, TickType_t wait = 0) {
  uint8_t p[mt::link::kMaxPayload];
  mt::link::Writer w(p, sizeof p);
  encode(m, w);
  return w.ok() && sendRaw(t, p, w.size(), wait);
}

// UI task, blocking: sends a request and waits for a reply of type replyType carrying the request's
// seq (the same on every try); up to 3 tries of timeoutMs each. reply gets the payload (cap kMaxPayload), replyLen its size.
bool request(mt::link::Msg type, const uint8_t* p, int n, mt::link::Msg replyType, uint8_t* reply, int& replyLen,
             uint32_t timeoutMs = 200);

// UI task, blocking: a request answered by a long job on the synth board (bank). The reply has the
// request's type and seq; meanwhile the board sends Progress frames with that seq (onProgress gets
// each). Nothing heard for idleMs: the request goes again with the same seq (the board answers a
// retry with a Progress, or its reply again), up to 3 times, then false.
using ProgressFn = void (*)(const mt::link::Progress& p, void* ctx);
bool requestLong(mt::link::Msg type, const uint8_t* p, int n, uint8_t* reply, int& replyLen,
                 ProgressFn onProgress = nullptr, void* ctx = nullptr, uint32_t idleMs = 2500);

// Hello seen and a Status within the last second.
bool synthUp();
// The synth speaks our protocol with the same SynthModel layout.
bool versionOk();
const char* synthFw();
uint16_t synthProtocol();
// Changes whenever the synth board (re)boots; 0 before the first Hello.
uint32_t bootId();

// StateSet replies for the mirror pump (UI task).
struct Reply {
  bool ok;  // Ack, else Nack
  uint16_t id;
};
bool takeReply(Reply& r);

// Synth figures from the Status frames since the previous take (UI task).
struct StatusAgg {
  uint32_t frames;     // Status frames
  uint32_t cpuSum;     // sum of cpuPct
  uint8_t cpuPeak;
  uint32_t stalls;
  uint16_t outPeak;    // 0..32767
  uint8_t trackPeak[16];  // 100 = full scale
};
StatusAgg takeStatus();
// The newest scope from Status (meters on): n samples, int8, every 2nd output sample.
int scope(int8_t* out, int cap);

struct Counters {
  uint32_t lost, late, crcErrors, retries, dropped;  // dropped: events the ESP could not queue
};
Counters counters();

// UI task: prints the synth's Log lines and the link counters now and then.
void pollLog();

}  // namespace slink
