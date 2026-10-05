#pragma once
#include <stdint.h>
#include "sample_bank.h"
#include "synth.h"

namespace audio {

// Sample bank in the "samples" partition, mounted by audio::begin(). Reads (list, data) from any
// task; changes only through the functions below (UI task), which pause the audio task while
// they write flash.
mt::SampleBank& bank();
bool bankMounted();
// Lookup by name for the synth (audio task).
const mt::SampleSource* sampleSource();

enum class BankResult : uint8_t {
  Ok, Busy, NoSd, NoBank, OpenFail, ReadFail, NotWav, Unsupported, Truncated, Exists, Full, WriteFail, NoMemory
};
const char* bankResultText(BankResult r);  // short, upper case, for toasts

// done / total in work units (bytes of the source file for import, bytes moved for compact).
using BankProgressFn = void (*)(uint32_t done, uint32_t total, void* ctx);

// Imports a WAV from the SD card as name (unique, <= 16 chars): mono, <= 32 kHz, root note from
// the "smpl" chunk or 60. Busy while the engine plays; Exists if the name is taken and !replace.
// replace: the WAV is checked first and imported under a temporary name, then takes the old
// sample's name in one table save, so a bad file or a failure keeps the old sample. Only when
// both do not fit is the old one removed before the import.
BankResult importWav(const char* path, const char* name, bool replace = false, BankProgressFn cb = nullptr,
                     void* ctx = nullptr);
// Reads the first maxMs of a WAV from the SD card for preview: mono, <= 32 kHz, into a new PSRAM
// buffer (*data, heap_caps_free it). Works while the engine plays.
BankResult loadWavPreview(const char* path, uint32_t maxMs, int16_t** data, uint32_t* frames, uint32_t* rate);
BankResult removeSample(int i);
BankResult compactBank(BankProgressFn cb = nullptr, void* ctx = nullptr);

// audio.cpp -> bank.cpp
void bankBegin();

}  // namespace audio
