#pragma once
#include <stdint.h>
#include "link_frame.h"

// Link messages: frame type ids and payload codecs (little-endian), shared by both firmwares.
namespace mt::link {

constexpr uint16_t kProtocol = 20;  // 8: SynthModel Tuning chunk; 9: xfade + Scenes chunk; 10: WtRender;
                                    // 11: export (RenderOut, RenderStartReq / RenderEndReq / RenderRep fields);
                                    // 12: LOFI (InstrType::Lofi, Instrument lofiFormula / lofiSync / lfoByte);
                                    // 13: recording (Rec*)
                                    // 14: one-shot (RenderStartReq::src, RenderEndReq normalize 2)
                                    // 15: FM SAT (Instrument::fmSat; the model's layout)
                                    // 16: CHIP SID / AY waves, SID filter, CHIP STEP (Instrument layout)
                                    // 17: FsCopy
                                    // 18: REC -> SD (RecStartReq::toSd, RecLevelRep::left, RecStopRep::ended,
                                    //     RecResult::CardFull)
                                    // 19: KIT 16 lanes (SynthModel size)
                                    // 20: live monitor metronome event (0xF9 accent level)

// Frame types. Append only: the numbers are the protocol.
enum class Msg : uint8_t {
  Hello = 1, Time, Ev, StateSet, Ack, Nack,
  FsOpen, FsRead, FsWrite, FsClose, FsStat, FsList, FsRemove, FsRename, FsMkdir,
  BankSync, AssetsSave, BankImport, SampleInfo, BankIndex, BankClear,
  WavePeaks, Onsets,
  PreviewNote, PreviewSlice, PreviewFile, PreviewStop,
  RenderStart, RenderBlocks, RenderEnd,
  Meters, Phones, Profile,
  Status, Progress, FwFromFile, Log,
  FsRmdir,
  WtFrame,
  Perf,
  Debug,
  WtRender,
  RenderOut,
  RecStart, RecSet, RecLevel, RecStop, RecCancel,
  FsCopy,
};

// Fs* replies: err first, 0 = ok.
constexpr int16_t kErrOk = 0;
constexpr int16_t kErrNoCard = -1;     // no card in the synth board, or it does not mount
constexpr int16_t kErrNotFound = -2;
constexpr int16_t kErrExists = -3;     // mkdir / rename target already there
constexpr int16_t kErrIo = -4;         // the card failed the operation
constexpr int16_t kErrBadHandle = -5;
constexpr int16_t kErrTooMany = -6;    // all handles in use
constexpr int16_t kErrBadArg = -7;     // malformed request, bad path or mode
constexpr int16_t kErrIsDir = -8;      // opened a folder for writing
constexpr int16_t kErrNotEmpty = -9;
constexpr int16_t kErrLink = -10;      // ESP side: no reply from the synth board

class Writer {
 public:
  Writer(uint8_t* buf, int cap) : b_(buf), cap_(cap) {}
  void u8(uint8_t v) { put(&v, 1); }
  void u16(uint16_t v);
  void u32(uint32_t v);
  void u64(uint64_t v);
  void bytes(const void* d, int n) { put(d, n); }
  void str(const char* s);  // length byte + bytes (up to 255)
  int size() const { return n_; }
  bool ok() const { return ok_; }

 private:
  void put(const void* d, int n);
  uint8_t* b_;
  int cap_;
  int n_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* buf, int n) : b_(buf), n_(n) {}
  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  uint64_t u64();
  void bytes(void* d, int n);
  // Length byte + bytes into out (cap includes the terminator; longer strings are cut).
  void str(char* out, int cap);
  int left() const { return n_ - pos_; }
  bool ok() const { return ok_; }  // false after reading past the end

 private:
  bool take(int n);
  const uint8_t* b_;
  int n_;
  int pos_ = 0;
  bool ok_ = true;
};

struct Hello {
  uint16_t proto = kProtocol;
  char fw[16] = {};
  uint32_t bootId = 0;
  uint32_t modelSize = 0;  // sizeof(SynthModel): both sides must agree on the mirror layout
  uint8_t perfSec = 0;     // seconds of PERF ring buffer the synth board holds, 0 = buffer effects silent
};

struct Time {
  uint64_t tUs = 0;  // ESP engine clock
};

struct Ev {
  uint64_t tUs;
  uint8_t track, len, b[3];
};
// Events for INT tracks; on the wire one base time + a 32-bit offset per event.
struct EvBatch {
  static constexpr int kMax = 40;
  uint8_t n = 0;
  Ev ev[kMax];
};

// One piece of a SynthModel chunk; a chunk larger than one frame is sent as pieces by offset.
struct StateSet {
  static constexpr int kMaxData = 480;
  uint16_t chunk = 0, total = 0, offset = 0, len = 0;  // total = the chunk's size
  uint8_t data[kMaxData];
};

struct Ack {
  uint16_t id = 0;  // chunk / block the reply is for
};
struct Nack {
  uint16_t id = 0;
  int16_t err = 0;
};

struct Status {
  static constexpr int kScopeMax = 256;
  uint8_t cpuPct = 0;
  uint16_t stalls = 0, late = 0;
  uint8_t voices = 0;
  uint16_t outPeak = 0;
  uint8_t trackPeak[16] = {};  // 100 = full scale at MAIN 100 %
  uint16_t lost = 0;
  uint8_t parks = 0;    // synth parks since boot (mod 256): each one resets the synth, PERF included
  uint16_t scopeN = 0;  // 0 while the meters are off; the newest output, every 2nd sample, >> 8
  int8_t scope[kScopeMax];
  // Sample streaming, since the previous Status (after the scope: protocol 3).
  uint8_t streamVoices = 0;        // pool slots voices hold now
  uint16_t underruns = 0;          // voices that ran out of data
  uint16_t minReserveMs = 0xFFFF;  // lowest reserve of a streaming voice, 0xFFFF = none streamed
  uint8_t psram = 0;               // 0 off (no chip / self-test), 1 on, 2 failed (watchdog)
  uint8_t psramFill = 0;           // % of the planned whole-sample copies made
  // Track strips (protocol 4): the trance gates' song clock in 1/256 sixteenths (0 .. 64 x 256),
  // kGateStopped while stopped; each track's envelope follower, 0..255 = 0..1.
  static constexpr uint16_t kGateStopped = 0xFFFF;
  uint16_t gatePos = kGateStopped;
  uint8_t follower[16] = {};
  // Protocol 5: the synth board's stack low-water mark since boot, bytes never touched (the main
  // loop and the audio interrupt share one stack in RAM1); 0 = the stack reached its guard,
  // kStackUnknown = not measured.
  static constexpr uint32_t kStackUnknown = 0xFFFFFFFF;
  uint32_t stackFree = kStackUnknown;
};

// A long request on the synth board is still running: op = its type, done / total in work units,
// item = the sample (0..127) or wavetable (128 + i) of the project it is on, kNoItem if none.
struct Progress {
  static constexpr uint8_t kNoItem = 0xFF;
  static constexpr uint8_t kWtItem = 128;
  uint8_t op = 0;
  uint32_t done = 0, total = 0;
  uint8_t item = kNoItem;
};

// Desk tools (link_desk.py; the synth board's desk build acts on them, others ignore them).
enum class DebugOp : uint8_t { PsramFault = 1, StreamBench = 2, StreamBenchStop = 3, Hang = 4 };
constexpr uint8_t kBenchSdOnly = 1;     // StreamBench b: no PSRAM copies, every voice streams
constexpr uint8_t kBenchSetupOnly = 2;  // StreamBench b: samples and instruments, no notes
constexpr uint8_t kBenchHeavy = 4;      // StreamBench b: 16 stream voices on 8 tracks + kFmVoiceMax heavy on 8
constexpr uint8_t kBenchWt = 8;         // StreamBench b, with kBenchHeavy: the heavy voices are wavetable SYNTH
constexpr uint8_t kBenchFx = 16;        // StreamBench b: drive, delay, reverb sends 100 on every voice, comp on
struct Debug {
  uint8_t op = 0;
  uint8_t a = 0, b = 0;  // StreamBench: a = semitones above the root, b = kBench* flags
  uint16_t secs = 0;     // StreamBench: how long the notes hold
};

// Meters (MIX shown: track peaks + scope in Status), Phones (output level 0..100 %).
struct Byte {
  uint8_t v = 0;
};

// PERF on the master: synth message 0xF6 op val on the preview track (see Synth), played now.
struct Perf {
  uint8_t op = 0, val = 0;
};

// Plays note with instrument instr on the synth's preview track for holdMs (0 = the default 300 ms),
// through the insert of pattern track `track` (>= 16: none; protocol 4).
struct PreviewNote {
  uint8_t instr = 0, note = 60;
  uint32_t holdMs = 0;
  uint8_t track = 0xFF;
};
// SAMPLE instrument instr: slice at root, held holdMs (up to 25 s) or until the next preview.
struct PreviewSlice {
  uint8_t instr = 0, slice = 0, root = 60;
  uint32_t holdMs = 0;
};

// Sample bank (on the synth board). Every request is answered by a frame of its type and seq;
// BankSync, AssetsSave, BankImport and BankClear may run long and send Progress every 100 ms until
// then. result: mt::BankResult (bank_ops.h). Masks: bit i of the project's sample / wavetable list.
constexpr int kSampleMaskBytes = 16;  // kProjSamples bits
constexpr int kWtMaskBytes = 4;       // kProjWavetables bits
inline bool maskGet(const uint8_t* m, int i) { return (m[i >> 3] >> (i & 7)) & 1; }
inline void maskSet(uint8_t* m, int i) { m[i >> 3] |= static_cast<uint8_t>(1 << (i & 7)); }

// BankSync: the mirrored lists against the cache, missing ones imported from /projects/<project>/
// ("" = no folder: only the cache). AssetsSave: the cached ones written there (overwritten).
struct BankProjectReq {
  char project[17] = {};
};
// missing: entries not cached (after the sync / so not written by the save), bits in the masks;
// failed: files the save could not write (result WriteFail then).
struct BankSetsRep {
  uint8_t result = 0;
  uint8_t missing = 0, failed = 0;
  uint8_t samples[kSampleMaskBytes] = {};
  uint8_t wavetables[kWtMaskBytes] = {};
};
// A WAV from the card into the cache: a sample (kind 0) or a wavetable (kind 1). crc = the data
// the caller expects (hasCrc): the file is not read when that data is cached already.
struct BankImportReq {
  uint8_t kind = 0;
  uint8_t hasCrc = 0;
  uint32_t crc = 0;
  char path[256] = {};
};
struct BankImportRep {
  uint8_t result = 0;
  uint32_t crc = 0, frames = 0, rate = 0;  // wavetable: frames = 0, rate = 0
  uint8_t root = 60;
};
// The bank entry of project sample index.
struct SampleInfoReq {
  uint8_t index = 0;
};
struct SampleInfoRep {
  uint8_t result = 0;
  uint8_t cached = 0;
  uint32_t frames = 0, rate = 0;
  uint8_t root = 60, loop = 0;
};
// BankIndex (empty request): the bank and the mirrored lists' state in it.
struct BankIndexRep {
  static constexpr int kRates = 128;  // kProjSamples
  uint8_t result = 0;  // NoBank: not mounted, the rest is zero
  uint8_t count = 0;   // entries
  uint32_t capacity = 0, free = 0, unused = 0;  // bytes; unused = cache the project does not use
  uint32_t gen = 0;    // SampleBank::generation
  uint8_t samples[kSampleMaskBytes] = {};  // cached
  uint8_t wavetables[kWtMaskBytes] = {};
  uint8_t builtins = 0;  // bit i: built-in wavetable i cached
  uint8_t n = 0;         // rates sent: the project's sample count
  uint16_t rate[kRates] = {};  // of each cached sample, 0 if missing
};
// BankClear: removes the entries the project does not use, or (compact) closes the holes.
struct BankClearReq {
  uint8_t compact = 0;
};
struct BankClearRep {
  uint8_t result = 0;
  uint8_t removed = 0;
};
// Level 0 of frame (0..63) of wavetable name (built-in "*NAME" or a name of the project's list).
struct WtFrameReq {
  uint8_t frame = 0;
  char name[17] = {};
};
struct WtFrameRep {
  static constexpr int kPoints = 256;  // kWtFrameLen
  uint8_t result = 0;
  int8_t pts[kPoints] = {};  // >> 8
};

// Sample editor, on the bank data of project sample index (result: mt::BankResult).
// WavePeaks: min / max (>> 8) of cols columns of a zoom grid from grid column col0, the grid putting
// span frames on width columns (mt::wavePeaks).
struct WavePeaksReq {
  uint8_t index = 0;
  uint32_t col0 = 0, span = 0;
  uint16_t width = 0;
  uint8_t cols = 0;
};
struct WavePeaksRep {
  static constexpr int kMaxCols = 240;
  uint8_t result = 0;
  uint8_t cols = 0;
  int8_t mn[kMaxCols] = {}, mx[kMaxCols] = {};
};
// Onsets: mt::detectOnsets of the whole sample (total, up to kMaxOnsets); a reply carries up to kMax
// of them from the skip-th on.
struct OnsetsReq {
  uint8_t index = 0;
  uint8_t skip = 0;
};
struct OnsetsRep {
  static constexpr int kMax = 80;
  uint8_t result = 0;
  uint8_t total = 0, n = 0;
  uint32_t pos[kMax] = {};
  uint16_t strength[kMax] = {};
};
// WtRender: a wavetable made (mt::WtMaker) from frames [from, to) of project sample index, written as
// a 64 x 256 WAV to path (under /wavetables/, through path.tmp). Progress meanwhile (done / total =
// frames of the table). result: mt::BankResult (sample access, card, memory); err: mt::WtMakeErr of the
// computation; pitched: CYCLE frames whose pitch was found.
struct WtRenderReq {
  uint8_t index = 0;
  uint32_t from = 0, to = 0;
  uint8_t mode = 0, norm = 0, seam = 1;  // mt::WtMode, mt::WtNorm, 0 / 1
  char path[64] = {};
};
struct WtRenderRep {
  uint8_t result = 0, err = 0, pitched = 0;
};
// PreviewFile: a WAV on the card (PCM 8/16/24 bit, channels mixed to mono, rate <= kPreviewMaxRate)
// streamed to the output from its start, ending the previous one; the reply has its frames and
// rate. PreviewStop (empty) ends it.
constexpr uint32_t kPreviewMaxRate = 48000;
struct PreviewFileReq {
  char path[256] = {};
};
struct PreviewFileRep {
  uint8_t result = 0;
  uint32_t frames = 0, rate = 0;
};

// Render (WAV / resample) on the synth board. RenderStart parks the live output, resets the synth and
// opens a temporary file for the bank (target bank: the mono mix goes into the cache at the end; path
// unused; kFile is refused: OpenFail; Busy while a REC take runs, any target). RenderBlocks carry the events of the next
// blocks (mt::OfflineSequence on the ESP), rendered and written before the reply; a block with more
// events than fit a frame goes on in the next one (kCont on its count, the frame's last record).
// RenderEnd finalises (or aborts: the file removed) and the live output resumes. Every reply is a
// RenderRep of the request's type; a request repeated with the same seq gets the same reply again.
enum class RenderResult : uint8_t {
  Ok, NoSd, OpenFail, WriteFail, ReadFail, Busy,
  NoRender,  // RenderBlocks / RenderEnd without a RenderStart (or after a timeout / Hello)
  BadOrder,  // RenderBlocks not starting at the block being filled
  Bank,      // the bank write failed: RenderRep::bank has the mt::BankResult
};
// Target kExport (mt::ExportWriter): path is the folder; RenderOut adds the pass's files (stereo, bits
// 16 / 24) before the first RenderBlocks; outs: the files wanted, the reply's outMax: how many this pass
// takes (the PERF ring in PSRAM: up to 20, RAM2: up to 6); skipFrames: not written (WRAP; also the bank
// target: Resample's wrap pass, out of the stats too); estFrames: the expected length (preallocation).
// src: kSrcMaster = the master mix; kSrcPreview (target kBank only, one-shot bounce) = the preview
// track's strip (its insert, fader, pan; no sends, no master: mt::RenderTaps::kPrev).
struct RenderStartReq {
  static constexpr uint8_t kFile = 0, kBank = 1, kExport = 2;
  static constexpr uint8_t kSrcMaster = 0, kSrcPreview = 1;
  uint8_t target = kFile;
  char path[128] = {};
  uint8_t bits = 24, outs = 1;
  uint32_t skipFrames = 0, estFrames = 0;
  uint8_t src = kSrcMaster;
};
// RenderOut: file idx (0.. in order, < outMax) of the export pass, tap (mt::kExportTapMaster, 1 + track,
// 17..19 MOD / DLY / RVB), its name in the folder. Reply: RenderRep.
struct RenderOut {
  uint8_t idx = 0, tap = 0;
  char name[64] = {};
};
struct RenderEv {
  uint8_t off, track, len, b[3];
};
struct RenderBlocks {
  static constexpr int kMaxBlocks = 8;
  static constexpr int kMaxEvents = 80;
  static constexpr uint8_t kCont = 0x80;
  uint32_t first = 0;  // block of the first record
  uint8_t n = 0;       // records
  uint8_t evN[kMaxBlocks] = {};
  RenderEv ev[kMaxEvents];
};
// normalize: 1 scaled so the peak lands at -1 dBFS (the bank: the mono mix's peak), 2 at -0.3 dBFS
// (mt::kNormPeakHot, one-shot); trim: cut after
// the last block above -60 dBFS.
// Export: frames = the files' length (0 = all written); the bank: at most frames kept (0 = no cut).
struct RenderEndReq {
  uint8_t abort = 0, normalize = 0, trim = 0;
  uint32_t frames = 0;
};
// blocks: rendered so far; at the end frames / crc of the result (the bank: its data key, file: the
// mono mix's crc in its "mtcr"), peak (before normalizing) and clips of the render.
// Export: quiet = rendered blocks since the last one above kSilence on the master; outMax (RenderStart);
// at RenderEnd the pass's outN files' peaks (int16 units) and clipped samples.
struct RenderRep {
  static constexpr int kMaxOut = 20;
  uint8_t result = 0;
  uint8_t bank = 0;
  uint32_t blocks = 0, frames = 0;
  uint16_t peak = 0;
  uint32_t clips = 0, crc = 0;
  uint32_t quiet = 0;
  uint8_t outMax = 0, outN = 0;
  uint16_t peaks[kMaxOut] = {};
  uint32_t clipsN[kMaxOut] = {};
};

// Recording (mt::RecCapture on the synth board) from MIC / LINE (the codec's inputs) or RESAMPLE (the
// synth's own output) into /samples/rec/RECn.wav while the song plays. RecStart arms a take (source
// mt::RecSource, gain mt::recGainClamp, thresh in dB or mt::kRecThreshOff, mon: input into the
// output; number: the first n free in the project, the board takes the first n' >= number with no
// file on the card and returns it). RecSet changes gain / mon (also before RecStart, with the source the
// meter follows: the window opens with one, the meter runs from then). RecLevel (no payload) polls state (mt::RecState), the meter since the last
// poll, frames so far; it is also the keepalive: 2 s without one and the board ends the take into
// the file without the bank. RecStop is long: Progress {op = RecStop} every 100 ms, then RecStopRep
// (the mono mix's crc / frames in the bank, the take's peak / clips). RecCancel (no payload) drops
// the take and its file. RecSet / RecCancel reply with a RecLevelRep.
// toSd (REC -> SD, from MIX): the take is only the file on the card, never the store: no 5-minute
// limit, it ends on its own at the WAV's 4 GB or when the card's free space (taken at RecStart) is
// down to mt::kRecSdReserveBytes (mt::recSdLimitFrames); RecStopRep has number / frames / peak /
// clips, bank and crc 0. Either kind ending on its own shows RecState::Saving until the RecStop, whose
// reply says why in ended.
enum class RecResult : uint8_t {
  Ok, NoSd, Busy, NoCodec,
  NoPsram,   // no PSRAM for the ring: no take
  NotRec,    // RecStop without a take
  TooShort,  // under mt::kRecMinFrames: nothing kept
  CardSlow,  // the ring overflowed: the take stopped, what was written is kept
  WriteFail,  // the card failed
  Bank,      // the bank import failed: RecStopRep::bank has the mt::BankResult
  BadArg,
  CardFull,  // RecStart toSd: under a second fits on the card
};
// RecStopRep::ended: how the capture ended before the reply.
enum class RecEnd : uint8_t {
  Stop,    // the RecStop (or a CardSlow / WriteFail end: see result)
  Length,  // on its own at its maximum: 5 min (store take) or the WAV's 4 GB (toSd)
  Space,   // toSd: on its own, the card's free space down to mt::kRecSdReserveBytes
};
struct RecStartReq {
  uint8_t source = 0;
  int8_t gain = 0, thresh = -128;
  uint8_t mon = 0;
  uint16_t number = 1;
  uint8_t toSd = 0;  // 1: REC -> SD (see above); left out by an older sender: 0
};
struct RecStartRep {
  uint8_t result = 0;
  uint16_t number = 0;  // n of the take's file
};
struct RecSetReq {
  int8_t gain = 0;
  uint8_t mon = 0;
  uint8_t source = 0;  // mt::RecSource: the input the meter (and the codec) follows before RecStart;
                      // during a take only the take's source is accepted (else BadArg)
};
struct RecLevelRep {
  uint8_t result = 0, state = 0;
  uint16_t peak = 0;
  uint32_t clips = 0, frames = 0;
  uint8_t codec = 0;  // the codec answers: MIC / LINE available
  uint32_t left = 0;  // frames before the take ends on its own (time / 4 GB / card space); 0 without one
};
struct RecStopRep {
  uint8_t result = 0, bank = 0;
  uint16_t number = 0;
  uint32_t crc = 0, frames = 0;
  uint16_t peak = 0;
  uint32_t clips = 0;
  uint8_t ended = 0;  // RecEnd
};

// FwFromFile: a .hex on the card (Intel HEX of this board's firmware) into the OTA area of the flash,
// checked (record checksums, size, the image's header and target id); Progress (bytes of the file)
// meanwhile. Ok: the reply goes out, then the image replaces the running firmware and the board
// reboots (a new Hello). Any error: the OTA area erased, the running firmware untouched.
enum class FwResult : uint8_t { Ok, NoSd, OpenFail, ReadFail, BadHex, TooBig, BadImage, FlashFail, Busy };
struct FwFromFileReq {
  char path[128] = {};
};
struct FwRep {
  uint8_t result = 0;
  uint32_t bytes = 0;  // the image
};

// Profile (PROJ -> SYS -> CPU profile): Byte request, v 1 = start (clears the sums), 0 = stop. The
// reply: the render cycles and counts per Synth::ProfStage summed since the start, the blocks
// rendered, the slowest block (events, render, preview mix) and the cycles per us to convert them.
struct ProfileRep {
  static constexpr int kStages = 25;  // Synth::kProfStages
  uint8_t running = 0;
  uint32_t blocks = 0;
  uint32_t cyclesPerUs = 0;
  uint32_t peakCycles = 0;
  uint64_t cycles[kStages] = {};
  uint32_t count[kStages] = {};
};

struct Log {
  static constexpr int kMax = 200;
  char text[kMax + 1] = {};
};

// Payload -> bytes; w.ok() false when it does not fit.
void encode(const Hello& m, Writer& w);
void encode(const Time& m, Writer& w);
void encode(const EvBatch& m, Writer& w);
void encode(const StateSet& m, Writer& w);
void encode(const Ack& m, Writer& w);
void encode(const Nack& m, Writer& w);
void encode(const Status& m, Writer& w);
void encode(const Progress& m, Writer& w);
void encode(const Debug& m, Writer& w);
void encode(const Log& m, Writer& w);
void encode(const Byte& m, Writer& w);
void encode(const Perf& m, Writer& w);
void encode(const PreviewNote& m, Writer& w);
void encode(const ProfileRep& m, Writer& w);
void encode(const PreviewSlice& m, Writer& w);
void encode(const BankProjectReq& m, Writer& w);
void encode(const BankSetsRep& m, Writer& w);
void encode(const BankImportReq& m, Writer& w);
void encode(const BankImportRep& m, Writer& w);
void encode(const SampleInfoReq& m, Writer& w);
void encode(const SampleInfoRep& m, Writer& w);
void encode(const BankIndexRep& m, Writer& w);
void encode(const BankClearReq& m, Writer& w);
void encode(const BankClearRep& m, Writer& w);
void encode(const WtFrameReq& m, Writer& w);
void encode(const WtFrameRep& m, Writer& w);
void encode(const WavePeaksReq& m, Writer& w);
void encode(const WavePeaksRep& m, Writer& w);
void encode(const OnsetsReq& m, Writer& w);
void encode(const OnsetsRep& m, Writer& w);
void encode(const WtRenderReq& m, Writer& w);
void encode(const WtRenderRep& m, Writer& w);
void encode(const PreviewFileReq& m, Writer& w);
void encode(const PreviewFileRep& m, Writer& w);
void encode(const RenderStartReq& m, Writer& w);
void encode(const RenderBlocks& m, Writer& w);
void encode(const RenderEndReq& m, Writer& w);
void encode(const RenderRep& m, Writer& w);
void encode(const RenderOut& m, Writer& w);
void encode(const FwFromFileReq& m, Writer& w);
void encode(const FwRep& m, Writer& w);
void encode(const RecStartReq& m, Writer& w);
void encode(const RecStartRep& m, Writer& w);
void encode(const RecSetReq& m, Writer& w);
void encode(const RecLevelRep& m, Writer& w);
void encode(const RecStopRep& m, Writer& w);

// False on a truncated / malformed payload.
bool decode(Reader& r, Hello& m);
bool decode(Reader& r, Time& m);
bool decode(Reader& r, EvBatch& m);
bool decode(Reader& r, StateSet& m);
bool decode(Reader& r, Ack& m);
bool decode(Reader& r, Nack& m);
bool decode(Reader& r, Status& m);
bool decode(Reader& r, Progress& m);
bool decode(Reader& r, Debug& m);
bool decode(Reader& r, Log& m);
bool decode(Reader& r, Byte& m);
bool decode(Reader& r, Perf& m);
bool decode(Reader& r, PreviewNote& m);
bool decode(Reader& r, PreviewSlice& m);
bool decode(Reader& r, BankProjectReq& m);
bool decode(Reader& r, BankSetsRep& m);
bool decode(Reader& r, BankImportReq& m);
bool decode(Reader& r, BankImportRep& m);
bool decode(Reader& r, SampleInfoReq& m);
bool decode(Reader& r, SampleInfoRep& m);
bool decode(Reader& r, BankIndexRep& m);
bool decode(Reader& r, BankClearReq& m);
bool decode(Reader& r, BankClearRep& m);
bool decode(Reader& r, WtFrameReq& m);
bool decode(Reader& r, WtFrameRep& m);
bool decode(Reader& r, WavePeaksReq& m);
bool decode(Reader& r, WavePeaksRep& m);
bool decode(Reader& r, OnsetsReq& m);
bool decode(Reader& r, OnsetsRep& m);
bool decode(Reader& r, WtRenderReq& m);
bool decode(Reader& r, WtRenderRep& m);
bool decode(Reader& r, PreviewFileReq& m);
bool decode(Reader& r, PreviewFileRep& m);
bool decode(Reader& r, ProfileRep& m);
bool decode(Reader& r, RenderStartReq& m);
bool decode(Reader& r, RenderBlocks& m);
bool decode(Reader& r, RenderEndReq& m);
bool decode(Reader& r, RenderRep& m);
bool decode(Reader& r, RenderOut& m);
bool decode(Reader& r, FwFromFileReq& m);
bool decode(Reader& r, FwRep& m);
bool decode(Reader& r, RecStartReq& m);
bool decode(Reader& r, RecStartRep& m);
bool decode(Reader& r, RecSetReq& m);
bool decode(Reader& r, RecLevelRep& m);
bool decode(Reader& r, RecStopRep& m);

// Encodes m as a whole frame of type t into out (kMaxEncoded bytes); 0 when it does not fit.
template <class M>
int frame(Msg t, uint8_t seq, const M& m, uint8_t* out) {
  uint8_t p[kMaxPayload];
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? encode(static_cast<uint8_t>(t), seq, p, w.size(), out) : 0;
}

}  // namespace mt::link
