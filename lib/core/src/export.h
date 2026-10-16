#pragma once
#include <stdint.h>
#include "model.h"
#include "render.h"
#include "synth.h"

// Export (FILE -> PROJECTS -> Export...): one offline render on the synth board writes the master and,
// optionally, the dry track stems and the MOD / DLY / RVB returns (Synth taps, RenderTaps) as stereo
// WAV files of one length into /export/<project>/<src>/. The ESP plans (outputs, names, lengths,
// passes); the board writes (ExportWriter). See docs/plans/2026-10-15-song-export-design.md.
namespace mt {

enum class ExportTail : uint8_t { Auto, Off, S2, S4, S8, Wrap, Count };
enum class ExportStems : uint8_t { Off, Tracks, TracksFx, Count };

constexpr int kExportMaxOut = RenderTaps::kCount + 1;  // MASTER + 16 tracks + 3 returns
constexpr uint8_t kExportTapMaster = 0;                // tap ids: 0 MASTER, 1..16 track, 17..19 MOD / DLY / RVB
constexpr int kExportNameMax = 64;                     // file name, with ".wav"
constexpr uint32_t kExportQuietBlocks = 345;           // AUTO: 1 s below kSilence ends the render
constexpr uint32_t kExportAutoPadBlocks = 35;          // AUTO: kept after the last loud block (0.1 s)
constexpr uint32_t kExportAutoMaxBlocks = 10336;       // AUTO: at most 30 s of tail

struct ExportSpec {
  RenderSpec::Mode mode = RenderSpec::Mode::Pattern;
  uint8_t pattern = 0;
  uint16_t tracksMask = 0xFFFF;  // what sounds in the master (ALL / SOLOED)
  uint8_t loops = 1;             // Mode::Pattern: 1, 2, 4, 8, 16
  ExportTail tail = ExportTail::Auto;
  ExportStems stems = ExportStems::Off;
  uint8_t bits = 24;  // 24 or 16
};

struct ExportOut {
  uint8_t tap = 0;  // kExportTapMaster, 1 + track, 17 MOD, 18 DLY, 19 RVB
  char name[kExportNameMax] = {};
};

// Frames of us on the render's virtual clock (a block of kRenderBlockUs = kBlock frames), rounded.
uint32_t usToFrames(uint64_t us);
// The sequence of an export: loops (+1 for WRAP), the tail blocks to render at most.
RenderSpec exportRenderSpec(const Project& p, const ExportSpec& s);
// Frames of the body (loops x pass or the song), of the skipped first pass (WRAP, else 0).
uint32_t exportBodyFrames(const Project& p, const ExportSpec& s);
uint32_t exportSkipFrames(const Project& p, const ExportSpec& s);
// The files' length in frames (after the skip) once `rendered` blocks are out, the last `quiet` of them
// below kSilence on the master (AUTO); never more than what was rendered.
uint32_t exportCutFrames(const Project& p, const ExportSpec& s, uint32_t rendered, uint32_t quiet);
// The length to expect before the render (AUTO: body + 2 s), for the estimate and preallocation.
uint32_t exportEstFrames(const Project& p, const ExportSpec& s);
// AUTO: the ESP stops sending blocks (body done, a second of silence or the 30 s cap).
bool exportAutoDone(const Project& p, const ExportSpec& s, uint32_t rendered, uint32_t quiet);

// INT tracks that get a stem: audible (mute / solo, tracksMask), with notes in the source's patterns.
uint16_t exportStemTracks(const Project& p, const ExportSpec& s);
// MIDI tracks with notes in the source's patterns (not exported).
int exportMidiTracks(const Project& p, const ExportSpec& s);
// "SONG", "P01", "P03x4".
void exportSrcTag(const ExportSpec& s, char* out, int n);
// /export/<project>/<src>
void exportDir(const Project& p, const ExportSpec& s, char* out, int n);
// The files of the export (MASTER first), at most kExportMaxOut; the count.
int exportOutputs(const Project& p, const ExportSpec& s, ExportOut* out);
// Passes for n outputs at outMax per pass.
int exportPasses(int n, int outMax);
// Bytes of n files of frames stereo frames at bits (with their 44-byte headers).
uint64_t exportBytes(int n, uint32_t frames, uint8_t bits);

// A block of one output as PCM into the file buffer: 24 bit (x 256, rounded) or 16 bit (TPDF dither,
// deterministic xorshift per output), clamped; peak (int16 units, 32767 max) and clipped samples.
struct ExportPcm {
  uint8_t bits = 24;
  uint32_t rng = 1;
  uint16_t peak = 0;
  uint32_t clips = 0;
  void seed(uint32_t s) { rng = s ? s : 1; }
  // n stereo frames of l / r (int16 scale) x g into out (n x frameBytes()); returns bytes written.
  uint32_t put(const float* l, const float* r, int n, float g, uint8_t* out);
  uint32_t frameBytes() const { return bits / 8u * 2u; }
};

// The card side of an ExportWriter (SdFat on the synth board, POSIX in the tests).
class ExportIo {
 public:
  virtual ~ExportIo() = default;
  // Creates / truncates path for output idx; prealloc: a size hint (failure ignored).
  virtual bool open(int idx, const char* path, uint64_t prealloc) = 0;
  virtual bool write(int idx, const void* d, uint32_t n) = 0;  // appended
  // Cuts the file at size, writes hdr at 0 and closes it.
  virtual bool seal(int idx, uint64_t size, const void* hdr, uint32_t hdrN) = 0;
  virtual void close(int idx) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;  // replaces to
};

// The synth board's end of an export pass: outputs added (each a <dir>/<name>.tmp), every rendered block
// converted into the outputs' buffers (cap bytes each, from mem), written in pieces of at least
// threshold() bytes, one write per output per flush(); end() cuts all to one length, writes the headers
// and renames. The taps' float buffers (tapMem: 2 x kBlock floats per output) are the writer's.
class ExportWriter {
 public:
  explicit ExportWriter(ExportIo& io) : io_(io) {}
  // A new pass. mem: maxOut x cap bytes; tapMem: maxOut x 2 x kBlock floats.
  void begin(const char* dir, uint8_t bits, uint32_t skipFrames, uint8_t* mem, uint32_t cap, float* tapMem,
             int maxOut);
  // Output idx (0..maxOut-1, in order) for tap with the file name; false: bad index / tap, open failed.
  bool add(int idx, uint8_t tap, const char* name, uint64_t prealloc);
  RenderTaps& taps() { return taps_; }
  int outs() const { return n_; }
  // The synth's last block (master and taps) into the buffers; false: a write failed.
  bool block(const Synth& s);
  // Writes every buffer over threshold(); false: a write failed.
  bool flush();
  // Cuts every file at frames (0 = all written), headers, renames; false: a write / rename failed (the
  // .tmp files removed).
  bool end(uint32_t frames);
  void abort();  // .tmp files closed and removed
  uint16_t peak(int i) const { return pcm_[i].peak; }
  uint32_t clips(int i) const { return pcm_[i].clips; }
  uint32_t threshold() const { return cap_ - kBlockRoom; }
  uint64_t written(int i) const { return done_[i]; }  // data bytes in the file (buffer not counted)

  static constexpr uint32_t kBlockRoom = 8 * Synth::kBlock * 6;  // a RenderBlocks frame at 24 bit

 private:
  void path(int i, bool tmp, char* out, int n) const;
  bool write(int i);
  ExportIo& io_;
  RenderTaps taps_;
  char dir_[128] = {};
  char name_[kExportMaxOut][kExportNameMax] = {};
  uint8_t tap_[kExportMaxOut] = {};
  ExportPcm pcm_[kExportMaxOut];
  uint8_t* buf_[kExportMaxOut] = {};
  uint32_t fill_[kExportMaxOut] = {};
  uint64_t done_[kExportMaxOut] = {};
  float* tapMem_ = nullptr;
  uint32_t cap_ = 0, skip_ = 0;
  uint8_t bits_ = 24;
  int n_ = 0, max_ = 0;
  bool open_ = false;
};

}  // namespace mt
