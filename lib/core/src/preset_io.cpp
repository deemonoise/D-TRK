#include "preset_io.h"
#include <string.h>

namespace mt {

bool savePreset(const Instrument& m, ByteSink& out) {
  uint8_t b[kPresetSize] = {'M', 'T', 'I', '1', kPresetVersion, static_cast<uint8_t>(m.type), 0, 0};
  size_t p = 8;
  packInst(m, b + p);
  p += kInstRecSize;
  packFm(m, b + p);
  p += kFmRecSize;
  packFlt(m, b + p);
  p += kFltRecSize;
  packSlices(m, b + p);
  p += kSliceRecSize;
  packSyn(m, b + p);
  p += kSynRecSize;
  packLfo(m, b + p);
  p += kLfoRecSize;
  const uint32_t c = crc32(b, p);
  b[p] = static_cast<uint8_t>(c);
  b[p + 1] = static_cast<uint8_t>(c >> 8);
  b[p + 2] = static_cast<uint8_t>(c >> 16);
  b[p + 3] = static_cast<uint8_t>(c >> 24);
  return out.write(b, sizeof(b));
}

LoadErr loadPreset(ByteSource& in, Instrument& out) {
  uint8_t b[kPresetSize];
  if (!in.read(b, 8)) return LoadErr::Truncated;
  if (memcmp(b, "MTI1", 4) != 0) return LoadErr::BadMagic;
  if (b[4] > kPresetVersion) return LoadErr::BadVersion;
  const size_t size = b[4] >= 5 ? kPresetSize : (b[4] >= 3 ? kPresetSizeV3 : (b[4] == 2 ? kPresetSizeV2 : kPresetSizeV1));
  if (!in.read(b + 8, size - 8)) return LoadErr::Truncated;
  const size_t p = size - 4;
  const uint32_t c = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
  if (c != crc32(b, p)) return LoadErr::BadCrc;
  Instrument m;
  unpackInst(b + 8, m);
  unpackFm(b + 8 + kInstRecSize, m);
  unpackFlt(b + 8 + kInstRecSize + kFmRecSize, m);
  const size_t slc = 8 + kInstRecSize + kFmRecSize + kFltRecSize;
  if (size >= kPresetSizeV2) unpackSlices(b + slc, m);
  if (size >= kPresetSizeV3) unpackSyn(b + slc + kSliceRecSize, m);
  if (size >= kPresetSize) unpackLfo(b + slc + kSliceRecSize + kSynRecSize, m);
  fixInstrument(m);
  out = m;
  return LoadErr::Ok;
}

void applyPreset(Instrument& dst, const Instrument& src, bool sampleFound) {
  char sample[kSampleNameMax + 1];
  memcpy(sample, dst.sample, sizeof(sample));
  const uint8_t root = dst.root;
  const uint8_t sliceCount = dst.sliceCount;
  uint16_t slices[kMaxSlices];
  memcpy(slices, dst.slices, sizeof(slices));
  dst = src;
  if (src.type == InstrType::Sample && !sampleFound) {
    memcpy(dst.sample, sample, sizeof(sample));
    dst.root = root;
    dst.sliceCount = sliceCount;
    memcpy(dst.slices, slices, sizeof(slices));
  }
}

}  // namespace mt
