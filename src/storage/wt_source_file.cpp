#include "wt_source_file.h"
#include <stdio.h>
#include "hw/sdcard.h"
#include "wt_make.h"
#include "sample_set.h"
#include "wav.h"
namespace storage {
namespace {
bool writeTemporary(const char* path, const int16_t* source) {
  fs::File file = hw::sdFs().open(path, FILE_WRITE);
  struct Sink final : mt::ByteSink {
    fs::File& file;
    explicit Sink(fs::File& f) : file(f) {}
    bool write(const void* data, size_t size) override {
      return file.write(static_cast<const uint8_t*>(data), size) == size;
    }
  } sink(file);
  const bool ok = file && mt::wtWavWrite(source, sink);
  file.close();
  return ok;
}
bool verifyTemporary(const char* path, const int16_t* source) {
  fs::File file = hw::sdFs().open(path, FILE_READ);
  if (!file) return false;
  struct Close { fs::File& file; ~Close() { file.close(); } } close{file};
  hw::FileSource input(file);
  mt::WavInfo info;
  const uint32_t expectedCrc = mt::sampleCrc(source, mt::kWtSrcSamples);
  if (mt::wavParse(input, info) != mt::WavErr::Ok || info.channels != 1 || info.bits != 16 || info.isFloat ||
      info.rate != 44100 || info.frames() != mt::kWtSrcSamples || info.clmFrame != mt::kWtFrameLen ||
      !info.hasRoot || info.root != 60 || !info.hasCrc || info.crc != expectedCrc ||
      file.size() != mt::kWtWavBytes || info.dataOffset + info.dataBytes != file.size() ||
      !file.seek(info.dataOffset)) return false;
  // Canonical mtcr can hit the cache without reading any PCM. Always validate the card's payload
  // against the intended source before rotating or calling that importer.
  int16_t block[512];
  uint32_t crc = 0;
  for (uint32_t done = 0; done < mt::kWtSrcSamples;) {
    const uint32_t count = mt::kWtSrcSamples-done < 512 ? mt::kWtSrcSamples-done : 512;
    if (file.read(reinterpret_cast<uint8_t*>(block), count*sizeof(int16_t)) != count*sizeof(int16_t)) return false;
    crc = mt::sampleCrc(block, count, crc); done += count;
  }
  return crc == expectedCrc;
}

}
WtSourceSaveResult saveWtSourceFile(const char* path, const int16_t* source,
                                    WtSourceImport import, void* ctx) {
  if (!path || !source || !import) return WtSourceSaveResult::WriteFail;
  auto& fs = hw::sdFs();
  char temporary[80], backup[80];
  const int tn=snprintf(temporary,sizeof(temporary),"%s.tmp",path);
  const int bn=snprintf(backup,sizeof(backup),"%s.bak",path);
  if(tn<0||bn<0||tn>=static_cast<int>(sizeof(temporary))||bn>=static_cast<int>(sizeof(backup)))
    return WtSourceSaveResult::WriteFail;
  if (fs.exists(backup)) return WtSourceSaveResult::RecoveryNeeded;
  if (!fs.exists("/wavetables") && !fs.mkdir("/wavetables")) return WtSourceSaveResult::WriteFail;
  if (fs.exists(temporary) && !fs.remove(temporary)) return WtSourceSaveResult::WriteFail;
  bool ok = writeTemporary(temporary, source);
  WtSourceSaveResult result = ok ? WtSourceSaveResult::Ok : WtSourceSaveResult::WriteFail;
  if (ok && !verifyTemporary(temporary, source)) { ok=false;result=WtSourceSaveResult::ReadbackFail; }
  const bool had = fs.exists(path);
  bool backed = false;
  if (ok && had) { backed=fs.rename(path,backup);ok=backed; }
  if (ok) ok=fs.rename(temporary,path);
  if (ok && !import(path,ctx)) {ok=false;result=WtSourceSaveResult::ImportFail;}
  if (!ok) {
    bool restored=true;
    if(backed)restored=(!fs.exists(path)||fs.remove(path))&&fs.rename(backup,path);
    else if(!had&&fs.exists(path))fs.remove(path);
    if(fs.exists(temporary))fs.remove(temporary);
    if(!restored)return WtSourceSaveResult::RecoveryNeeded;
    return result==WtSourceSaveResult::Ok ? WtSourceSaveResult::WriteFail : result;
  }
  if(backed)fs.remove(backup);
  return WtSourceSaveResult::Ok;
}
}
