#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

using pcnt_unit_handle_t = int;
using pcnt_channel_handle_t = int;
using gpio_num_t = int;
struct pcnt_unit_config_t {
  int low_limit = 0, high_limit = 0;
  struct { bool accum_count = false; } flags;
};
struct pcnt_glitch_filter_config_t { int max_glitch_ns = 0; };
struct pcnt_chan_config_t { int edge_gpio_num = 0, level_gpio_num = 0; };
enum { PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
       PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE };
#define ESP_ERROR_CHECK(call) assert((call) == 0)
namespace pins { constexpr int kEncA = 11, kEncB = 12; }

pcnt_unit_handle_t unit;
int encAcc = 0;
uint8_t encDetentPhase = 0;
pcnt_unit_config_t config;
int raw = 0, accumulated = 0, phase = 3, clearCalls = 0, edgeOnRead = 0;
bool lowWatch = false, highWatch = false;
int pcnt_new_unit(const pcnt_unit_config_t* c, pcnt_unit_handle_t* u) { config = *c; *u = 1; return 0; }
int pcnt_unit_set_glitch_filter(int, const pcnt_glitch_filter_config_t*) { return 0; }
int pcnt_new_channel(int, const pcnt_chan_config_t*, pcnt_channel_handle_t* c) { *c = 1; return 0; }
int pcnt_channel_set_edge_action(int, int, int) { return 0; }
int pcnt_channel_set_level_action(int, int, int) { return 0; }
int gpio_pullup_en(int) { return 0; }
int gpio_get_level(int pin) { return pin == pins::kEncA ? phase & 1 : (phase >> 1) & 1; }
int pcnt_unit_add_watch_point(int, int value) {
  if (value == config.low_limit) lowWatch = true;
  if (value == config.high_limit) highWatch = true;
  return 0;
}
int pcnt_unit_enable(int) { return 0; }
int pcnt_unit_start(int) { return 0; }
int pcnt_unit_clear_count(int) { raw = accumulated = 0; ++clearCalls; return 0; }
void advance(int delta) {
  const int sign = delta < 0 ? -1 : 1;
  for (int i = 0; i < std::abs(delta); ++i) {
    raw += sign;
    if (raw == config.high_limit || raw == config.low_limit) {
      if (config.flags.accum_count && (raw > 0 ? highWatch : lowWatch)) accumulated += raw;
      raw = 0;
    }
  }
}
int pcnt_unit_get_count(int, int* value) {
  *value = raw + accumulated;
  advance(edgeOnRead);  // a real edge can arrive between reading and clearing the peripheral
  edgeOnRead = 0;
  return 0;
}

/* DECODER */

void expect(int wanted) {
  const int actual = readDetents();
  if (actual != wanted) {
    std::fprintf(stderr, "count=%d phase=%d: expected %d, got %d\n", raw + accumulated, phase, wanted, actual);
    std::abort();
  }
}
void sample(int count, int bits, int wanted) {
  raw = count; accumulated = 0; phase = bits; expect(wanted);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  setupEncoder();
  if (!std::strcmp(argv[1], "aligned")) {
    for (int sign : {-1, 1}) {
      for (int i = 0; i < 20; ++i) { advance(sign * 4); expect(sign); }
      advance(-sign * 4); expect(-sign); // the very first reverse click
    }
  } else if (!std::strcmp(argv[1], "offset")) {
    // A full cycle ends in the boot/rest phase, but the raw counter has gained/lost an edge.
    for (int offset : {-1, 1}) {
      for (int sign : {-1, 1}) {
        encAcc = 0;
        sample(sign * (4 + offset), 3, sign);
        for (int i = 0; i < 20; ++i) {
          sample(sign * offset, 3, -sign);
          sample(sign * (4 + offset), 3, sign);
        }
      }
    }
  } else if (!std::strcmp(argv[1], "offset_edges")) {
    // A stray edge at rest is ignored, then the step still completes at the physical detent.
    sample(1, 3, 0);
    for (int i = 0; i < 20; ++i) {
      sample(2, 2, 0); sample(3, 0, 0); sample(4, 1, 0); sample(5, 3, 1);
      sample(4, 1, 0); sample(3, 0, 0); sample(2, 2, 0); sample(1, 3, -1);
    }
  } else if (!std::strcmp(argv[1], "bounce")) {
    // Both directions, both common rest polarities. No event for a partial excursion and return.
    for (int rest : {0, 1, 2, 3}) {
      for (int sign : {-1, 1}) {
        phase = rest; setupEncoder(); encAcc = 0;
        const int a = rest ^ 1, b = rest ^ 3, c = rest ^ 2;
        sample(sign, a, 0); sample(0, rest, 0); sample(sign, a, 0);
        sample(sign * 2, b, 0); sample(sign, a, 0); sample(sign * 2, b, 0);
        sample(sign * 3, c, 0); sample(sign * 2, b, 0);
        sample(sign, a, 0); sample(0, rest, 0);
        sample(sign, a, 0); sample(sign * 2, b, 0); sample(sign * 3, c, 0);
        sample(sign * 4, rest, sign);
        sample(sign * 3, c, 0); sample(sign * 4, rest, 0); // bounce at the detent
        sample(sign * 5, a, 0); sample(sign * 6, b, 0); sample(sign * 7, c, 0);
        sample(sign * 6, b, 0); sample(sign * 5, a, 0); sample(sign * 4, rest, 0);
        sample(sign * 3, c, 0); sample(sign * 2, b, 0); sample(sign, a, 0);
        sample(0, rest, -sign);
      }
    }
  } else if (!std::strcmp(argv[1], "batch")) {
    sample(41, 3, 10); sample(1, 3, -10); sample(-39, 3, -10); sample(1, 3, 10);
  } else if (!std::strcmp(argv[1], "read_edge")) {
    // A sample straddling an edge must not consume a partial snapshot.
    raw = 4; edgeOnRead = 1;
    expect(0);
    sample(4, 3, 1);
    sample(0, 3, -1);
  } else if (!std::strcmp(argv[1], "overflow")) {
    // Exercise the old clear-at-800 boundary with an edge arriving just after the read.
    raw = 804; edgeOnRead = 1;
    int steps = readDetents();
    advance(3); steps += readDetents();
    assert(steps == 202);
    assert(clearCalls == 1); // initialization only; never clear a running counter
    // Repeated positive and negative hardware wraps keep the first reverse click intact.
    for (int sign : {-1, 1}) {
      for (int i = 0; i < 20000; ++i) { advance(sign * 4); expect(sign); }
      advance(-sign * 4); expect(-sign);
    }
  } else if (!std::strcmp(argv[1], "signed_wrap")) {
    const int top = std::numeric_limits<int32_t>::max();
    const int bottom = std::numeric_limits<int32_t>::min();
    encAcc = top - 1;
    sample(bottom + 2, 3, 1);
    sample(top - 1, 3, -1);
    sample(bottom + 2, 3, 1);
  } else { assert(false); }
}
