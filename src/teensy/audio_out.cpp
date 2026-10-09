#include "audio_out.h"
#include "clock.h"
#include "stream_ring.h"
#include "synth.h"

namespace {


// Sample offset of local time t in the block starting at blockUs; -1 when it lies past the block.
int offsetIn(uint64_t t, uint64_t blockUs) {
  if (t <= blockUs) return 0;
  const uint64_t off = ((t - blockUs) * static_cast<uint64_t>(AUDIO_SAMPLE_RATE_EXACT) + 500000) / 1000000;
  return off < AUDIO_BLOCK_SAMPLES ? static_cast<int>(off) : -1;
}

}  // namespace

void SynthStream::update() {
  audio_block_t* l = allocate();
  audio_block_t* r = allocate();
  if (!l || !r) {
    if (l) release(l);
    if (r) release(r);
    return;
  }
  const uint32_t c0 = ARM_DWT_CYCCNT;
  if (synth_ && parked_) {
    const uint64_t blockUs = micros64();
    while (const QEv* e = q_.peek()) {
      if (offsetIn(e->localUs, blockUs) < 0) break;
      q_.pop();  // not counted as lost: nothing plays while parked
    }
    memset(l->data, 0, sizeof(l->data));
    memset(r->data, 0, sizeof(r->data));
  } else if (synth_) {
    if (resetPending_) {
      synth_->reset();
      resetPending_ = false;
    }
    if (hook_) hook_(lastCycles_);
    // The block is stamped with the time it is computed: the output delay after that is constant,
    // and link::kPlayLatencyUs only has to cover one block period plus the link jitter.
    const uint64_t blockUs = micros64();
    while (const QEv* e = q_.peek()) {
      const int off = offsetIn(e->localUs, blockUs);
      if (off < 0) break;  // a later block
      if (e->localUs < blockUs) late_++;
      if (!synth_->event(off, e->track, e->b, e->len)) lost_++;
      q_.pop();
    }
    synth_->render(l->data, r->data);
  } else {
    memset(l->data, 0, sizeof(l->data));
    memset(r->data, 0, sizeof(r->data));
  }
  if (preview_) preview_->mix(l->data, r->data, AUDIO_BLOCK_SAMPLES, previewGain_);
  const uint32_t c = ARM_DWT_CYCCNT - c0;
  lastCycles_ = c;
  const uint32_t kBlockCycles = blockCycles();
  if (synth_) synth_->setLoad(static_cast<float>(c) / kBlockCycles);
  cycles_ += c;
  blocks_++;
  if (c > peakCycles_) peakCycles_ = c;
  if (c > kBlockCycles) stalls_++;
  uint16_t pk = outPeak_;
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
    const int a = abs(l->data[i]) > abs(r->data[i]) ? abs(l->data[i]) : abs(r->data[i]);
    if (a > pk) pk = static_cast<uint16_t>(a > 32767 ? 32767 : a);
  }
  outPeak_ = pk;
  if (scopeOn_) {  // mono sum
    int at = scopeAt_;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
      scope_[at] = static_cast<int16_t>((l->data[i] + r->data[i]) >> 1);
      at = (at + 1) % kScopeLen;
    }
    scopeAt_ = at;
  }
  const int32_t g = phonesQ15_;
  if (g < 32768) {
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
      l->data[i] = static_cast<int16_t>((l->data[i] * g) >> 15);
      r->data[i] = static_cast<int16_t>((r->data[i] * g) >> 15);
    }
  }
  transmit(l, 0);
  transmit(r, 1);
  release(l);
  release(r);
}

void SynthStream::park(bool on) {
  if (on) resetPending_ = true;
  parked_ = on;
}

SynthStream::Stats SynthStream::take() {
  __disable_irq();
  Stats s{cycles_, blocks_, peakCycles_, stalls_, late_, lost_, outPeak_};
  cycles_ = blocks_ = peakCycles_ = 0;
  stalls_ = late_ = lost_ = outPeak_ = 0;
  __enable_irq();
  return s;
}

void SynthStream::setPhones(uint8_t pct) {
  const int32_t v = pct > 100 ? 100 : pct;
  phonesQ15_ = v * v * 32768 / 10000;
}

int SynthStream::scopeRead(int8_t* out, int n) const {
  if (!scopeOn_) return 0;
  if (n > kScopeLen / 2) n = kScopeLen / 2;
  const int at = scopeAt_;  // read without a lock: a block may tear, as on the ESP
  for (int i = 0; i < n; ++i) out[i] = static_cast<int8_t>(scope_[(at - 2 * (n - i) + kScopeLen) % kScopeLen] >> 8);
  return n;
}
