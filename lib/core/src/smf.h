#pragma once
#include <stddef.h>
#include <stdint.h>

namespace mt {

// Standard MIDI File (format 0/1) note extractor. No heap: notes go to the caller's buffer.
struct SmfNote {
  uint32_t tick, len;  // file ticks
  uint8_t note, vel, src;  // src = index into SmfInfo::src
};

struct SmfSource {
  uint8_t track, channel;  // MTrk index (clamped to 255), MIDI channel 0..15
  char name[17];           // track name (meta 0x03), truncated, NUL-terminated
  uint16_t count;          // stored notes
  uint8_t lo, hi;          // pitch range of stored notes
};

constexpr int kSmfMaxSources = 32;

struct SmfInfo {
  uint16_t ppq;
  uint32_t firstTempoUsPerQ;  // 500000 by default
  uint8_t sourceCount;
  SmfSource src[kSmfMaxSources];
  uint32_t noteCount;
  uint32_t lastTick;  // max of end-of-track ticks and note ends
};

enum class SmfErr : uint8_t { Ok, NotMidi, Smpte, Truncated, TooManyNotes, Unsupported };

// Parses data into info and notes[0..cap). Notes are sorted by tick (stable: file order on ties).
// Source = (track, channel); sources beyond 32 are dropped. Overlapping same-pitch notes close
// FIFO; notes left open close at end of track.
// TooManyNotes: the first `cap` notes (in file order) are kept and info is filled.
// Truncated: everything parsed before the cut is kept and info is filled. A malformed track
// (bad status/data byte, tick past 2^32-1) also stops parsing with Truncated when at least one
// note was parsed before it; with no notes it is NotMidi. Running status survives meta/sysex
// events; real-time bytes (0xF8..0xFE) are skipped.
SmfErr parseSmf(const uint8_t* data, size_t size, SmfInfo& info, SmfNote* notes, uint32_t cap);

}  // namespace mt
