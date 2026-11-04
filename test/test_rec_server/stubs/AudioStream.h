#pragma once
#include "Arduino.h"
#define AUDIO_BLOCK_SAMPLES 128
#define AUDIO_SAMPLE_RATE_EXACT 44100.0f
struct audio_block_t { int16_t data[AUDIO_BLOCK_SAMPLES]; };
class AudioStream {
 public:
  AudioStream(unsigned char, audio_block_t**) {}
  virtual ~AudioStream() = default;
  virtual void update() = 0;
};
