#pragma once
#include <stdint.h>
#include "link_msg.h"

// Files over the link: the synth board serves its card (FsServerCore on an FsBackend), the ESP
// builds the requests and parses the replies (FsClientCore). A request is answered by a frame of the
// same type and seq. Payloads (str = length byte + bytes):
//   FsOpen   mode u8, path str           -> err i16, h u8, isDir u8, size u32 (a folder gets no handle)
//   FsRead   h u8, pos u32, n u16        -> err i16, bytes (up to kFsChunk, fewer at the end)
//   FsWrite  h u8, pos u32, bytes        -> err i16, written u16
//   FsClose  h u8                        -> err i16
//   FsStat   path str [, flags u8]       -> err i16, isDir u8, size u32 [, totalMb u32, freeMb u32]
//            (flags bit 0: the card's size and free space too; old requests without the byte work)
//   FsList   start u16, max u8, dir str  -> err i16, more u8, count u8, count x {isDir u8, size u32, name str}
//   FsRemove / FsMkdir / FsRmdir path    -> err i16 (FsRmdir: the folder and everything in it)
//   FsRename from str, to str            -> err i16
// Read and write carry the file position: a request done twice does the same thing twice.
namespace mt::link {

constexpr int kFsChunk = 480;    // FsRead / FsWrite data per frame
constexpr int kFsMaxOpen = 4;    // files open at once on the synth board
constexpr int kFsPathMax = 256;  // incl. the terminator

enum class FsMode : uint8_t { Read, Write, Append };  // Write creates / truncates

struct FsEntry {
  bool isDir = false;
  uint32_t size = 0;
  char name[kFsPathMax] = {};  // no path
};

// The card as the server sees it (SdFat on the synth board, POSIX in the tests). Absolute paths.
class FsBackend {
 public:
  virtual ~FsBackend() = default;
  // kErrOk when the card is in and mounted (remounts when it can), else kErrNoCard.
  virtual int16_t ready() = 0;
  virtual int16_t stat(const char* path, bool& isDir, uint32_t& size) = 0;
  // A file into slot 0..kFsMaxOpen-1 (free, chosen by the caller); size after opening.
  virtual int16_t open(int slot, const char* path, FsMode mode, uint32_t& size) = 0;
  virtual int read(int slot, uint32_t pos, uint8_t* d, int n) = 0;  // bytes (0 = end) or an error
  virtual int write(int slot, uint32_t pos, const uint8_t* d, int n) = 0;
  virtual int16_t close(int slot) = 0;
  // One listing at a time: the entries of path in the card's order, without "." and "..".
  virtual int16_t dirOpen(const char* path) = 0;
  virtual bool dirNext(FsEntry& e) = 0;
  virtual void dirClose() = 0;
  virtual int16_t remove(const char* path) = 0;
  virtual int16_t rename(const char* from, const char* to) = 0;
  virtual int16_t mkdir(const char* path) = 0;
  virtual int16_t rmdir(const char* path) = 0;  // an empty folder
  // The card's size and free space in MB (FsStat with kFsStatSpace).
  virtual int16_t space(uint32_t& totalMb, uint32_t& freeMb) {
    totalMb = freeMb = 0;
    return kErrIo;
  }
};

constexpr uint8_t kFsStatSpace = 1;  // FsStat flags

bool isFsRequest(Msg t);

class FsServerCore {
 public:
  explicit FsServerCore(FsBackend& b) : b_(b) {}
  // Request t (frame seq) -> the reply payload in out (kMaxPayload bytes); returns its size, -1 when
  // t is not an Fs* request. The last request again (same type and seq: the ESP retried) gets the
  // same reply without being done again.
  int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out);
  // Closes every file, drops the listing and the reply cache (the ESP restarted, the card went away).
  void reset();

 private:
  // body: where w writes (after the err).
  int run(Msg t, Reader& r, Writer& w, uint8_t* body);
  int16_t list(Reader& r, Writer& w, uint8_t* body);
  int16_t rmTree(const char* path, int depth);
  void dropList();

  FsBackend& b_;
  bool open_[kFsMaxOpen] = {};
  // Listing continued page by page: dir, the index of its next unsent entry, an entry read that did
  // not fit the last page.
  bool listing_ = false;
  char listDir_[kFsPathMax] = {};
  uint32_t listNext_ = 0;
  bool held_ = false;
  FsEntry heldEntry_;
  // Reply cache for retries.
  bool cached_ = false;
  uint8_t cacheType_ = 0, cacheSeq_ = 0;
  int cacheLen_ = 0;
  uint8_t cache_[kMaxPayload];
};

// One FsList reply: its entries in order.
class FsListPage {
 public:
  bool next(FsEntry& e);
  int count() const { return count_; }
  bool more() const { return more_; }

 private:
  friend class FsClientCore;
  uint8_t buf_[kMaxPayload];
  int len_ = 0, pos_ = 0;
  int count_ = 0, read_ = 0;
  bool more_ = false;
};

class FsClientCore {
 public:
  // Sends request t and waits for its reply (same type and seq) into reply (kMaxPayload); false when
  // none came.
  using Xfer = bool (*)(void* ctx, Msg t, const uint8_t* p, int n, uint8_t* reply, int& replyLen);
  FsClientCore(Xfer x, void* ctx) : x_(x), ctx_(ctx) {}

  // All return kErrOk / a negative error (kErrLink: no reply); read / write the byte count.
  int16_t open(const char* path, FsMode mode, uint8_t& h, bool& isDir, uint32_t& size);
  int read(uint8_t h, uint32_t pos, void* d, int n);  // n is cut to kFsChunk; 0 = end of file
  int write(uint8_t h, uint32_t pos, const void* d, int n);
  int16_t close(uint8_t h);
  int16_t stat(const char* path, bool& isDir, uint32_t& size);
  // The card's size and free space, MB.
  int16_t space(uint32_t& totalMb, uint32_t& freeMb);
  // Entries of dir from index start on, as many as fit one frame (at most max).
  int16_t list(const char* dir, uint16_t start, uint8_t max, FsListPage& page);
  int16_t remove(const char* path);
  int16_t rename(const char* from, const char* to);
  int16_t mkdir(const char* path);
  int16_t rmdir(const char* path);
  int16_t lastError() const { return last_; }

 private:
  // Sends req_[0..n) as t; on a reply r reads past its err. Returns the err.
  int16_t call(Msg t, int n, Reader& r);
  int16_t pathOp(Msg t, const char* path);

  Xfer x_;
  void* ctx_;
  int16_t last_ = kErrOk;
  uint8_t req_[kMaxPayload];
  uint8_t reply_[kMaxPayload];
  int replyLen_ = 0;
};

}  // namespace mt::link
