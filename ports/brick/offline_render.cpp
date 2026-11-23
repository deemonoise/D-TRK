#include "offline_render.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <unistd.h>
#include <vector>
#include <FS.h>
#include "audio/audio.h"
#include "brick_runtime.h"
#include "export.h"
#include "engine_native.h"
#include "link/bank_client.h"
#include "recorder.h"
#include "render_link.h"
#include "sample_store.h"

namespace brick {
namespace {
using namespace mt::link;

class ExportFiles final : public mt::ExportIo {
  std::array<FILE*, mt::kExportMaxOut> files_{};
 public:
  bool open(int slot, const char* path, uint64_t) override {
    files_[slot] = std::fopen(fs::resolve(path).c_str(), "w+b");
    return files_[slot] != nullptr;
  }
  bool write(int slot, const void* data, uint32_t size) override {
    return std::fwrite(data, 1, size, files_[slot]) == size;
  }
  bool seal(int slot, uint64_t size, const void* header, uint32_t bytes) override {
    FILE* file = files_[slot];
    const bool saved = std::fflush(file) == 0 && ftruncate(fileno(file), size) == 0 &&
                       std::fseek(file, 0, SEEK_SET) == 0 && write(slot, header, bytes);
    files_[slot] = nullptr;
    return std::fclose(file) == 0 && saved;
  }
  void close(int slot) override {
    if (files_[slot]) std::fclose(files_[slot]);
    files_[slot] = nullptr;
  }
  bool remove(const char* path) override { return std::remove(fs::resolve(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) override {
    return std::rename(fs::resolve(from).c_str(), fs::resolve(to).c_str()) == 0;
  }
};

class RenderSession {
 public:
  explicit RenderSession(const RenderStartReq& start) : request_(start), feeder_(synth()), writer_(files_) {}
  ~RenderSession() {
    writer_.abort();
    if (parked_) {
      synth().setTaps(nullptr);
      parkForRender(false);
    }
  }

  RenderResult begin(RenderRep& reply) {
    if (request_.target == RenderStartReq::kExport) {
      if (std::strncmp(request_.path, "/export/", 8) || !disk().mkdir(request_.path))
        return RenderResult::OpenFail;
      const int files = std::clamp<int>(request_.outs, 1, mt::kExportMaxOut);
      constexpr uint32_t chunk = 32768;
      buffers_.resize(files * chunk);
      taps_.resize(files * 2 * mt::Synth::kBlock);
      writer_.begin(request_.path, request_.bits, request_.skipFrames,
                    buffers_.data(), chunk, taps_.data(), files);
      reply.outMax = files;
    } else if (request_.target != RenderStartReq::kBank) {
      return RenderResult::OpenFail;
    }
    audio::previewStop();
    parkForRender(true);
    parked_ = true;
    feeder_.reset();
    if (request_.target == RenderStartReq::kExport) {
      synth().setTaps(&writer_.taps());
    } else {
      feeder_.setSkip(request_.skipFrames);
      if (request_.src == RenderStartReq::kSrcPreview) {
        constexpr int preview = mt::RenderTaps::kPrev;
        previewTap_.mask = 1u << preview;
        previewTap_.l[preview] = previewLeft_.data();
        previewTap_.r[preview] = previewRight_.data();
        synth().setTaps(&previewTap_);
        feeder_.setTap(&previewTap_, preview);
      }
    }
    return RenderResult::Ok;
  }

  RenderResult add(const RenderOut& out) {
    if (request_.target != RenderStartReq::kExport || std::strchr(out.name, '/'))
      return RenderResult::OpenFail;
    return writer_.add(out.idx, out.tap, out.name, 0) ? RenderResult::Ok : RenderResult::OpenFail;
  }

  RenderResult apply(const RenderBlocks& blocks, RenderRep& reply) {
    const auto status = feeder_.apply(blocks, [](const int16_t* l, const int16_t* r, int frames, void* ctx) {
      return static_cast<RenderSession*>(ctx)->block(l, r, frames);
    }, this);
    counters(reply);
    if (status == RenderResult::Ok && !writer_.flush()) return RenderResult::WriteFail;
    return status;
  }

  RenderResult end(const RenderEndReq& end, RenderRep& reply) {
    counters(reply);
    if (end.abort) return RenderResult::Ok;
    if (request_.target == RenderStartReq::kBank) return finishSample(end, reply);
    reply.outN = writer_.outs();
    for (int i = 0; i < reply.outN; ++i) {
      reply.peaks[i] = writer_.peak(i);
      reply.peak = std::max(reply.peak, reply.peaks[i]);
      reply.clipsN[i] = writer_.clips(i);
      reply.clips += reply.clipsN[i];
    }
    const auto total = static_cast<uint64_t>(reply.blocks) * mt::Synth::kBlock;
    const auto kept = total - std::min<uint64_t>(total, request_.skipFrames);
    reply.frames = static_cast<uint32_t>(std::min<uint64_t>(kept, end.frames ? end.frames : UINT32_MAX));
    return writer_.end(end.frames) ? RenderResult::Ok : RenderResult::WriteFail;
  }

 private:
  void counters(RenderRep& reply) const {
    reply.blocks = feeder_.blocks();
    reply.quiet = reply.blocks - feeder_.loudBlocks();
  }
  bool block(const int16_t* l, const int16_t* r, int frames) {
    if (request_.target == RenderStartReq::kExport) return writer_.block(synth());
    for (int i = 0; i < frames; ++i) mono_.push_back(static_cast<int16_t>((l[i] + r[i]) / 2));
    return true;
  }
  static bool readMono(int16_t* out, uint32_t pos, uint32_t n, void* ctx) {
    const auto& mono = static_cast<RenderSession*>(ctx)->mono_;
    if (pos > mono.size() || n > mono.size() - pos) return false;
    std::copy_n(mono.data() + pos, n, out);
    return true;
  }
  RenderResult finishSample(const RenderEndReq& end, RenderRep& reply) {
    size_t keep = mono_.size();
    if (end.trim) {
      const uint32_t loud = feeder_.loudBlocksMono() * mt::Synth::kBlock;
      const uint32_t audible = loud - std::min(loud, request_.skipFrames);
      keep = std::min<size_t>(keep, std::max<uint32_t>(mt::Synth::kBlock, audible));
    }
    if (end.frames) keep = std::min<size_t>(keep, end.frames);
    reply.peak = feeder_.monoPeak();
    reply.clips = feeder_.clips();
    if (end.normalize)
      mt::normalizeSamples(mono_.data(), keep, reply.peak, end.normalize == 2 ? mt::kNormPeakHot : mt::kNormPeak);

    mt::ImportOut out;
    auto result = mt::storeWriteFrames(disk(), keep, mt::kSynthRate, readMono, this, out);
    if (result == mt::BankResult::Ok)
      result = mt::bankWriteFrames(bank(), *audio::mirrorProject(), keep, mt::kSynthRate, readMono, this, out);
    reply.bank = static_cast<uint8_t>(result);
    reply.crc = out.crc;
    reply.frames = out.frames;
    return result == mt::BankResult::Ok ? RenderResult::Ok : RenderResult::Bank;
  }

  bool parked_ = false;
  RenderStartReq request_;
  mt::RenderFeeder feeder_;
  ExportFiles files_;
  mt::ExportWriter writer_;
  std::vector<uint8_t> buffers_;
  std::vector<float> taps_;
  std::vector<int16_t> mono_;
  mt::RenderTaps previewTap_{};
  std::array<float, mt::Synth::kBlock> previewLeft_, previewRight_;
};

// storage::Session holds the project mutex throughout every synchronous offline pass.
std::unique_ptr<RenderSession> session;
RenderResult dispatch(Msg type, Reader& r, RenderRep& reply) {
  if (type == Msg::RenderStart) {
    RenderStartReq start;
    if (!decode(r, start)) return RenderResult::OpenFail;
    session.reset();
    if (recording()) return RenderResult::Busy;
    if (!disk().ready()) return RenderResult::NoSd;
    session = std::make_unique<RenderSession>(start);
    return session->begin(reply);
  }
  if (!session) return RenderResult::NoRender;
  if (type == Msg::RenderBlocks) {
    RenderBlocks blocks;
    return decode(r, blocks) ? session->apply(blocks, reply) : RenderResult::ReadFail;
  }
  if (type == Msg::RenderOut) {
    RenderOut out;
    return decode(r, out) ? session->add(out) : RenderResult::OpenFail;
  }
  RenderEndReq end;
  return type == Msg::RenderEnd && decode(r, end) ? session->end(end, reply) : RenderResult::ReadFail;
}
}  // namespace

int renderRequest(mt::link::Msg type, const uint8_t* data, int size, uint8_t* payload) {
  std::lock_guard<ProjectMutex> guard(projectMutex());
  Reader reader(data, size);
  RenderRep reply;
  try {
    reply.result = static_cast<uint8_t>(dispatch(type, reader, reply));
  } catch (const std::bad_alloc&) {
    reply.result = static_cast<uint8_t>(RenderResult::WriteFail);
  }
  if (type == Msg::RenderEnd || reply.result != static_cast<uint8_t>(RenderResult::Ok)) session.reset();
  Writer writer(payload, kMaxPayload);
  encode(reply, writer);
  return writer.ok() ? writer.size() : 0;
}
}  // namespace brick
