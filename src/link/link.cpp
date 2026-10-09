#include "link.h"
#include <Arduino.h>
#include <atomic>
#include <string.h>
#include "engine/engine.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "link_uart.h"
#include "synth_model.h"

using namespace mt::link;

namespace slink {

namespace {

constexpr uint32_t kTimeMs = 50;
constexpr uint32_t kHelloMs = 500;
constexpr uint32_t kAliveMs = 1000;  // no Status for this long: the synth is down
constexpr int kBulkDepth = 8;
constexpr int kLogLines = 8;

// Engine -> link task, single producer / single consumer.
struct PEv {
  uint64_t t;
  uint8_t track, len, b[3];
};
constexpr uint32_t kEvRing = 512;
PEv evRing[kEvRing];
std::atomic<uint32_t> evHead{0}, evTail{0};
std::atomic<uint32_t> dropped{0};

struct Bulk {
  uint16_t n;
  uint8_t d[kMaxEncoded];
};
QueueHandle_t bulkQ = nullptr;
QueueHandle_t replyQ = nullptr;
std::atomic<uint8_t> seqCounter{0};
std::atomic<uint32_t> crcErrors{0}, retries{0};

// Guarded by mux: filled by the link task, read by the UI task.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Hello hello;
bool helloSeen = false;
uint32_t lastStatusMs = 0;
StatusAgg agg = {};
int8_t scopeBuf[Status::kScopeMax];
int scopeN = 0;
uint32_t lostTotal = 0, lateTotal = 0;
char logLines[kLogLines][Log::kMax + 1];
int logHead = 0, logCount = 0;

// The one outstanding request (UI task waits, link task completes it).
SemaphoreHandle_t reqMutex = nullptr;
SemaphoreHandle_t reqDone = nullptr;
volatile bool reqActive = false;
volatile uint8_t reqSeq = 0;
volatile uint8_t reqType = 0;
uint8_t* reqBuf = nullptr;
volatile int reqLen = 0;
// requestLong: the newest Progress of the outstanding request (guarded by mux).
Progress progLatest;
bool progNew = false;

void writeFrame(Msg t, const uint8_t* p, int n) {
  uint8_t buf[kMaxEncoded];
  const int k = encode(static_cast<uint8_t>(t), seqCounter.fetch_add(1), p, n, buf);
  if (k > 0) uartWrite(buf, k);
}

template <class M>
void writeMsg(Msg t, const M& m) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(m, w);
  if (w.ok()) writeFrame(t, p, w.size());
}

void sendHello() {
  Hello h;
  strncpy(h.fw, DTRK_REV, sizeof h.fw - 1);
  h.modelSize = sizeof(mt::SynthModel);
  writeMsg(Msg::Hello, h);
}

// Everything the engine queued, in batches of up to EvBatch::kMax.
void flushEvents() {
  static EvBatch b;
  for (;;) {
    b.n = 0;
    uint32_t tail = evTail.load(std::memory_order_relaxed);
    const uint32_t head = evHead.load(std::memory_order_acquire);
    while (tail != head && b.n < EvBatch::kMax) {
      const PEv& e = evRing[tail % kEvRing];
      b.ev[b.n++] = {e.t, e.track, e.len, {e.b[0], e.b[1], e.b[2]}};
      ++tail;
    }
    evTail.store(tail, std::memory_order_release);
    if (b.n == 0) return;
    writeMsg(Msg::Ev, b);
  }
}

void onStatus(Reader& r) {
  static Status s;
  if (!decode(r, s)) return;
  portENTER_CRITICAL(&mux);
  lastStatusMs = millis();
  agg.frames++;
  agg.cpuSum += s.cpuPct;
  if (s.cpuPct > agg.cpuPeak) agg.cpuPeak = s.cpuPct;
  agg.stalls += s.stalls;
  if (s.outPeak > agg.outPeak) agg.outPeak = s.outPeak;
  for (int t = 0; t < 16; ++t)
    if (s.trackPeak[t] > agg.trackPeak[t]) agg.trackPeak[t] = s.trackPeak[t];
  lostTotal += s.lost;
  lateTotal += s.late;
  scopeN = s.scopeN;
  memcpy(scopeBuf, s.scope, s.scopeN);
  portEXIT_CRITICAL(&mux);
}

void dispatch(const Decoder& d) {
  if (reqActive && d.type() == reqType && d.seq() == reqSeq) {
    const int n = d.size() < kMaxPayload ? d.size() : kMaxPayload;
    memcpy(reqBuf, d.payload(), n);
    reqLen = n;
    reqActive = false;
    xSemaphoreGive(reqDone);
    return;
  }
  Reader r(d.payload(), d.size());
  if (reqActive && static_cast<Msg>(d.type()) == Msg::Progress && d.seq() == reqSeq) {
    Progress pr;
    if (!decode(r, pr)) return;
    portENTER_CRITICAL(&mux);
    progLatest = pr;
    progNew = true;
    portEXIT_CRITICAL(&mux);
    return;
  }
  switch (static_cast<Msg>(d.type())) {
    case Msg::Hello: {
      Hello h;
      if (!decode(r, h)) return;
      portENTER_CRITICAL(&mux);
      hello = h;
      helloSeen = true;
      lastStatusMs = millis();
      portEXIT_CRITICAL(&mux);
      break;
    }
    case Msg::Status: onStatus(r); break;
    case Msg::Ack:
    case Msg::Nack: {
      Reply rep{static_cast<Msg>(d.type()) == Msg::Ack, 0};
      if (rep.ok) {
        Ack a;
        if (!decode(r, a)) return;
        rep.id = a.id;
      } else {
        Nack n;
        if (!decode(r, n)) return;
        rep.id = n.id;
      }
      xQueueSend(replyQ, &rep, 0);
      break;
    }
    case Msg::Log: {
      Log m;
      if (!decode(r, m)) return;
      portENTER_CRITICAL(&mux);
      memcpy(logLines[(logHead + logCount) % kLogLines], m.text, sizeof m.text);
      if (logCount < kLogLines) logCount++;
      else logHead = (logHead + 1) % kLogLines;
      portEXIT_CRITICAL(&mux);
      break;
    }
    default: break;
  }
}

bool alive(uint32_t now) {
  portENTER_CRITICAL(&mux);
  const bool up = helloSeen && now - lastStatusMs < kAliveMs;
  portEXIT_CRITICAL(&mux);
  return up;
}

void run(void*) {
  static Decoder dec;
  static uint8_t rx[512];
  static Bulk bulk;
  uint32_t lastTime = 0, lastHello = 0;
  for (;;) {
    const int n = uartRead(rx, sizeof rx, 1);
    for (int i = 0; i < n; ++i)
      if (dec.feed(rx[i])) dispatch(dec);
    crcErrors.store(dec.errors(), std::memory_order_relaxed);
    flushEvents();
    const uint32_t now = millis();
    // The link starts before the engine (the card is on the synth board): no Time until its clock runs.
    if (now - lastTime >= kTimeMs) {
      lastTime = now;
      if (const uint64_t t = engine::nowUs()) writeMsg(Msg::Time, Time{t});
    }
    if (!alive(now) && now - lastHello >= kHelloMs) {
      lastHello = now;
      sendHello();
    }
    if (xQueueReceive(bulkQ, &bulk, 0) == pdTRUE) uartWrite(bulk.d, bulk.n);
  }
}

bool queueFrame(Msg t, uint8_t seq, const uint8_t* p, int n, TickType_t wait) {
  static Bulk b;  // UI task only
  b.n = static_cast<uint16_t>(encode(static_cast<uint8_t>(t), seq, p, n, b.d));
  return b.n > 0 && bulkQ && xQueueSend(bulkQ, &b, wait) == pdTRUE;
}

}  // namespace

void begin() {
  bulkQ = xQueueCreate(kBulkDepth, sizeof(Bulk));
  replyQ = xQueueCreate(16, sizeof(Reply));
  reqMutex = xSemaphoreCreateMutex();
  reqDone = xSemaphoreCreateBinary();
  if (!uartBegin(kBaud)) {
    Serial.println("link: uart init failed");
    return;
  }
  xTaskCreatePinnedToCore(run, "link", 6144, nullptr, configMAX_PRIORITIES - 3, nullptr, 0);
}

void postEvent(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  const uint32_t head = evHead.load(std::memory_order_relaxed);
  if (head - evTail.load(std::memory_order_acquire) >= kEvRing) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  PEv& e = evRing[head % kEvRing];
  e.t = t;
  e.track = track;
  e.len = len > 3 ? 3 : len;
  memcpy(e.b, b, e.len);
  evHead.store(head + 1, std::memory_order_release);
}

bool sendRaw(Msg t, const uint8_t* payload, int n, TickType_t wait) {
  return queueFrame(t, seqCounter.fetch_add(1), payload, n, wait);
}

bool request(Msg type, const uint8_t* p, int n, Msg replyType, uint8_t* reply, int& replyLen, uint32_t timeoutMs) {
  if (!reqMutex || xSemaphoreTake(reqMutex, pdMS_TO_TICKS(timeoutMs * 3)) != pdTRUE) return false;
  bool ok = false;
  // One seq for all tries: a repeat is answered from the synth's reply cache, not done twice, and a
  // late reply to an earlier try counts.
  const uint8_t seq = seqCounter.fetch_add(1);
  xSemaphoreTake(reqDone, 0);  // a late reply of an earlier request
  for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
    if (attempt) retries.fetch_add(1, std::memory_order_relaxed);
    reqBuf = reply;
    reqType = static_cast<uint8_t>(replyType);
    reqSeq = seq;
    reqActive = true;
    if (!queueFrame(type, seq, p, n, pdMS_TO_TICKS(timeoutMs))) {
      reqActive = false;
      continue;
    }
    ok = xSemaphoreTake(reqDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    reqActive = false;
  }
  if (ok) replyLen = reqLen;
  xSemaphoreGive(reqMutex);
  return ok;
}

bool requestLong(Msg type, const uint8_t* p, int n, uint8_t* reply, int& replyLen, ProgressFn onProgress, void* ctx,
                 uint32_t idleMs) {
  if (!reqMutex || xSemaphoreTake(reqMutex, pdMS_TO_TICKS(idleMs)) != pdTRUE) return false;
  const uint8_t seq = seqCounter.fetch_add(1);
  xSemaphoreTake(reqDone, 0);
  portENTER_CRITICAL(&mux);
  progNew = false;
  portEXIT_CRITICAL(&mux);
  reqBuf = reply;
  reqType = static_cast<uint8_t>(type);
  reqSeq = seq;
  reqActive = true;
  bool ok = false;
  int tries = 0;
  bool sent = queueFrame(type, seq, p, n, pdMS_TO_TICKS(200));
  uint32_t heard = millis();
  for (;;) {
    if (xSemaphoreTake(reqDone, pdMS_TO_TICKS(50)) == pdTRUE) {
      ok = true;
      break;
    }
    Progress pr;
    portENTER_CRITICAL(&mux);
    const bool got = progNew;
    if (got) pr = progLatest;
    progNew = false;
    portEXIT_CRITICAL(&mux);
    const uint32_t now = millis();
    if (got) {
      heard = now;
      if (onProgress) onProgress(pr, ctx);
    }
    if (sent && now - heard < idleMs) continue;
    if (++tries >= 3) break;
    retries.fetch_add(1, std::memory_order_relaxed);
    sent = queueFrame(type, seq, p, n, pdMS_TO_TICKS(200));
    heard = now;
  }
  reqActive = false;
  if (ok) replyLen = reqLen;
  xSemaphoreGive(reqMutex);
  return ok;
}

bool synthUp() { return alive(millis()); }

bool versionOk() {
  portENTER_CRITICAL(&mux);
  const bool ok = helloSeen && hello.proto == kProtocol && hello.modelSize == sizeof(mt::SynthModel);
  portEXIT_CRITICAL(&mux);
  return ok;
}

const char* synthFw() {
  static char fw[sizeof hello.fw];
  portENTER_CRITICAL(&mux);
  memcpy(fw, helloSeen ? hello.fw : "-", helloSeen ? sizeof fw : 2);
  portEXIT_CRITICAL(&mux);
  return fw;
}

uint16_t synthProtocol() {
  portENTER_CRITICAL(&mux);
  const uint16_t p = helloSeen ? hello.proto : 0;
  portEXIT_CRITICAL(&mux);
  return p;
}

uint32_t bootId() {
  portENTER_CRITICAL(&mux);
  const uint32_t b = helloSeen ? hello.bootId : 0;
  portEXIT_CRITICAL(&mux);
  return b;
}

bool takeReply(Reply& r) { return replyQ && xQueueReceive(replyQ, &r, 0) == pdTRUE; }

StatusAgg takeStatus() {
  portENTER_CRITICAL(&mux);
  const StatusAgg a = agg;
  agg = {};
  portEXIT_CRITICAL(&mux);
  return a;
}

int scope(int8_t* out, int cap) {
  portENTER_CRITICAL(&mux);
  const int n = scopeN < cap ? scopeN : cap;
  memcpy(out, scopeBuf + (scopeN - n), n);
  portEXIT_CRITICAL(&mux);
  return n;
}

Counters counters() {
  portENTER_CRITICAL(&mux);
  Counters c{lostTotal, lateTotal, 0, 0, 0};
  portEXIT_CRITICAL(&mux);
  c.crcErrors = crcErrors.load(std::memory_order_relaxed);
  c.retries = retries.load(std::memory_order_relaxed);
  c.dropped = dropped.load(std::memory_order_relaxed);
  return c;
}

void pollLog() {
  char line[Log::kMax + 1];
  for (;;) {
    portENTER_CRITICAL(&mux);
    const bool any = logCount > 0;
    if (any) {
      memcpy(line, logLines[logHead], sizeof line);
      logHead = (logHead + 1) % kLogLines;
      logCount--;
    }
    portEXIT_CRITICAL(&mux);
    if (!any) break;
    Serial.printf("synth: %s\n", line);
  }
  static uint32_t lastPrint = 0;
  static Counters prev = {};
  const uint32_t now = millis();
  if (now - lastPrint < 5000) return;
  lastPrint = now;
  const Counters c = counters();
  if (memcmp(&c, &prev, sizeof c) != 0)
    Serial.printf("link: lost %lu late %lu crc %lu retries %lu dropped %lu\n", static_cast<unsigned long>(c.lost),
                  static_cast<unsigned long>(c.late), static_cast<unsigned long>(c.crcErrors),
                  static_cast<unsigned long>(c.retries), static_cast<unsigned long>(c.dropped));
  prev = c;
}

}  // namespace slink
