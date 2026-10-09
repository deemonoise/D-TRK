#include "link_fs.h"
#include <string.h>

namespace mt::link {

namespace {

constexpr uint8_t kNoHandle = 0xFF;  // FsOpen of a folder
constexpr int kRmDepth = 8;          // FsRmdir: folder levels below the one removed

// Path argument: absolute, not cut by the length byte.
bool readPath(Reader& r, char* out) {
  r.str(out, kFsPathMax);
  return r.ok() && out[0] == '/';
}

bool joinPath(const char* dir, const char* name, char* out) {
  const size_t d = strlen(dir), n = strlen(name);
  const bool slash = d > 0 && dir[d - 1] == '/';
  if (d + (slash ? 0 : 1) + n + 1 > static_cast<size_t>(kFsPathMax)) return false;
  memcpy(out, dir, d);
  size_t o = d;
  if (!slash) out[o++] = '/';
  memcpy(out + o, name, n + 1);
  return true;
}

}  // namespace

bool isFsRequest(Msg t) {
  switch (t) {
    case Msg::FsOpen:
    case Msg::FsRead:
    case Msg::FsWrite:
    case Msg::FsClose:
    case Msg::FsStat:
    case Msg::FsList:
    case Msg::FsRemove:
    case Msg::FsRename:
    case Msg::FsMkdir:
    case Msg::FsRmdir: return true;
    default: return false;
  }
}

int FsServerCore::handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out) {
  if (!isFsRequest(t)) return -1;
  const uint8_t type = static_cast<uint8_t>(t);
  if (cached_ && cacheType_ == type && cacheSeq_ == seq) {
    memcpy(out, cache_, cacheLen_);
    return cacheLen_;
  }
  Reader r(p, n);
  Writer w(out + 2, kMaxPayload - 2);
  int16_t err = b_.ready();
  if (err != kErrOk) reset();  // the files were on the card that went away
  else err = static_cast<int16_t>(run(t, r, w, out + 2));
  const int len = 2 + (err == kErrOk && w.ok() ? w.size() : 0);
  if (err == kErrOk && !w.ok()) err = kErrBadArg;
  out[0] = static_cast<uint8_t>(err);
  out[1] = static_cast<uint8_t>(static_cast<uint16_t>(err) >> 8);
  cached_ = true;
  cacheType_ = type;
  cacheSeq_ = seq;
  cacheLen_ = len;
  memcpy(cache_, out, len);
  return len;
}

void FsServerCore::reset() {
  for (int i = 0; i < kFsMaxOpen; ++i) {
    if (open_[i]) b_.close(i);
    open_[i] = false;
  }
  dropList();
  cached_ = false;
}

void FsServerCore::dropList() {
  if (listing_) b_.dirClose();
  listing_ = false;
  held_ = false;
}

int FsServerCore::run(Msg t, Reader& r, Writer& w, uint8_t* body) {
  char path[kFsPathMax];
  uint8_t data[kFsChunk];
  if (t != Msg::FsList && t != Msg::FsStat && t != Msg::FsRead) dropList();  // may change a folder
  switch (t) {
    case Msg::FsOpen: {
      const uint8_t mode = r.u8();
      if (!readPath(r, path) || mode > static_cast<uint8_t>(FsMode::Append)) return kErrBadArg;
      bool isDir = false;
      uint32_t size = 0;
      const int16_t e = b_.stat(path, isDir, size);
      if (e == kErrOk && isDir) {
        if (mode != static_cast<uint8_t>(FsMode::Read)) return kErrIsDir;
        w.u8(kNoHandle);
        w.u8(1);
        w.u32(0);
        return kErrOk;
      }
      if (e != kErrOk && (e != kErrNotFound || mode == static_cast<uint8_t>(FsMode::Read))) return e;
      int slot = 0;
      while (slot < kFsMaxOpen && open_[slot]) ++slot;
      if (slot == kFsMaxOpen) return kErrTooMany;
      const int16_t o = b_.open(slot, path, static_cast<FsMode>(mode), size);
      if (o != kErrOk) return o;
      open_[slot] = true;
      w.u8(static_cast<uint8_t>(slot));
      w.u8(0);
      w.u32(size);
      return kErrOk;
    }
    case Msg::FsRead:
    case Msg::FsWrite:
    case Msg::FsClose: {
      const uint8_t h = r.u8();
      if (!r.ok()) return kErrBadArg;
      if (h >= kFsMaxOpen || !open_[h]) return kErrBadHandle;
      if (t == Msg::FsClose) {
        open_[h] = false;
        return b_.close(h);
      }
      const uint32_t pos = r.u32();
      if (t == Msg::FsRead) {
        int n = r.u16();
        if (!r.ok()) return kErrBadArg;
        if (n > kFsChunk) n = kFsChunk;
        const int k = b_.read(h, pos, data, n);
        if (k < 0) return k;
        w.bytes(data, k);
        return kErrOk;
      }
      const int n = r.left();
      if (!r.ok() || n > kFsChunk) return kErrBadArg;
      r.bytes(data, n);
      const int k = b_.write(h, pos, data, n);
      if (k < 0) return k;
      w.u16(static_cast<uint16_t>(k));
      return kErrOk;
    }
    case Msg::FsStat: {
      if (!readPath(r, path)) return kErrBadArg;
      bool isDir = false;
      uint32_t size = 0;
      const uint8_t flags = r.left() > 0 ? r.u8() : 0;
      const int16_t e = b_.stat(path, isDir, size);
      if (e != kErrOk) return e;
      w.u8(isDir ? 1 : 0);
      w.u32(size);
      if (flags & kFsStatSpace) {
        uint32_t total = 0, free = 0;
        if (const int16_t se = b_.space(total, free); se != kErrOk) return se;
        w.u32(total);
        w.u32(free);
      }
      return kErrOk;
    }
    case Msg::FsList: return list(r, w, body);
    case Msg::FsRemove:
      if (!readPath(r, path)) return kErrBadArg;
      return b_.remove(path);
    case Msg::FsRename: {
      char to[kFsPathMax];
      if (!readPath(r, path) || !readPath(r, to)) return kErrBadArg;
      return b_.rename(path, to);
    }
    case Msg::FsMkdir:
      if (!readPath(r, path)) return kErrBadArg;
      return b_.mkdir(path);
    case Msg::FsRmdir:
      if (!readPath(r, path) || strcmp(path, "/") == 0) return kErrBadArg;
      return rmTree(path, 0);
    default: return kErrBadArg;
  }
}

int16_t FsServerCore::list(Reader& r, Writer& w, uint8_t* body) {
  const uint16_t start = r.u16();
  const uint8_t max = r.u8();
  char dir[kFsPathMax];
  if (!readPath(r, dir)) return kErrBadArg;
  if (!listing_ || strcmp(dir, listDir_) != 0 || start != listNext_) {
    dropList();
    const int16_t e = b_.dirOpen(dir);
    if (e != kErrOk) return e;
    listing_ = true;
    memcpy(listDir_, dir, sizeof listDir_);
    listNext_ = 0;
    while (listNext_ < start && b_.dirNext(heldEntry_)) ++listNext_;
  }
  // more, count at body[0..1], filled in at the end.
  w.u8(0);
  w.u8(0);
  int count = 0;
  while (listNext_ >= start && count < max) {
    if (!held_ && !b_.dirNext(heldEntry_)) break;
    held_ = true;
    const size_t nameLen = strlen(heldEntry_.name);
    const int need = 6 + static_cast<int>(nameLen > 255 ? 255 : nameLen);
    if (w.size() + need > kMaxPayload - 2) break;
    w.u8(heldEntry_.isDir ? 1 : 0);
    w.u32(heldEntry_.size);
    w.str(heldEntry_.name);
    held_ = false;
    ++listNext_;
    ++count;
  }
  // Reads one ahead to tell whether there is more.
  if (listNext_ >= start && !held_ && b_.dirNext(heldEntry_)) held_ = true;
  body[0] = held_ ? 1 : 0;
  body[1] = static_cast<uint8_t>(count);
  return kErrOk;
}

int16_t FsServerCore::rmTree(const char* path, int depth) {
  if (depth > kRmDepth) return kErrBadArg;
  bool isDir = false;
  uint32_t size = 0;
  int16_t e = b_.stat(path, isDir, size);
  if (e != kErrOk) return e;
  if (!isDir) return kErrNotFound;
  char child[kFsPathMax];
  FsEntry ent;
  // One entry at a time, the listing reopened after each removal (folders here hold few files).
  for (int guard = 0; guard < 100000; ++guard) {
    e = b_.dirOpen(path);
    if (e != kErrOk) return e;
    const bool any = b_.dirNext(ent);
    b_.dirClose();
    if (!any) return b_.rmdir(path);
    if (!joinPath(path, ent.name, child)) return kErrBadArg;
    e = ent.isDir ? rmTree(child, depth + 1) : b_.remove(child);
    if (e != kErrOk) return e;
  }
  return kErrIo;
}

bool FsListPage::next(FsEntry& e) {
  if (read_ >= count_) return false;
  Reader r(buf_ + pos_, len_ - pos_);
  e.isDir = r.u8() != 0;
  e.size = r.u32();
  r.str(e.name, sizeof e.name);
  if (!r.ok()) {
    read_ = count_;
    return false;
  }
  pos_ = len_ - r.left();
  ++read_;
  return true;
}

int16_t FsClientCore::call(Msg t, int n, Reader& r) {
  replyLen_ = 0;
  if (!x_(ctx_, t, req_, n, reply_, replyLen_)) return last_ = kErrLink;
  r = Reader(reply_, replyLen_);
  const int16_t e = static_cast<int16_t>(r.u16());
  return last_ = r.ok() ? e : kErrBadArg;
}

int16_t FsClientCore::pathOp(Msg t, const char* path) {
  Writer w(req_, kMaxPayload);
  if (strlen(path) >= static_cast<size_t>(kFsPathMax)) return last_ = kErrBadArg;
  w.str(path);
  Reader r(nullptr, 0);
  return call(t, w.size(), r);
}

int16_t FsClientCore::open(const char* path, FsMode mode, uint8_t& h, bool& isDir, uint32_t& size) {
  if (strlen(path) >= static_cast<size_t>(kFsPathMax)) return last_ = kErrBadArg;
  Writer w(req_, kMaxPayload);
  w.u8(static_cast<uint8_t>(mode));
  w.str(path);
  Reader r(nullptr, 0);
  const int16_t e = call(Msg::FsOpen, w.size(), r);
  if (e != kErrOk) return e;
  h = r.u8();
  isDir = r.u8() != 0;
  size = r.u32();
  return last_ = r.ok() ? kErrOk : kErrBadArg;
}

int FsClientCore::read(uint8_t h, uint32_t pos, void* d, int n) {
  if (n > kFsChunk) n = kFsChunk;
  Writer w(req_, kMaxPayload);
  w.u8(h);
  w.u32(pos);
  w.u16(static_cast<uint16_t>(n));
  Reader r(nullptr, 0);
  const int16_t e = call(Msg::FsRead, w.size(), r);
  if (e != kErrOk) return e;
  const int k = r.left() < n ? r.left() : n;
  r.bytes(d, k);
  return k;
}

int FsClientCore::write(uint8_t h, uint32_t pos, const void* d, int n) {
  if (n > kFsChunk) n = kFsChunk;
  Writer w(req_, kMaxPayload);
  w.u8(h);
  w.u32(pos);
  w.bytes(d, n);
  Reader r(nullptr, 0);
  const int16_t e = call(Msg::FsWrite, w.size(), r);
  if (e != kErrOk) return e;
  const int k = r.u16();
  if (!r.ok()) return last_ = kErrBadArg;
  return k;
}

int16_t FsClientCore::close(uint8_t h) {
  req_[0] = h;
  Reader r(nullptr, 0);
  return call(Msg::FsClose, 1, r);
}

int16_t FsClientCore::stat(const char* path, bool& isDir, uint32_t& size) {
  const int16_t e = pathOp(Msg::FsStat, path);
  if (e != kErrOk) return e;
  Reader r(reply_ + 2, replyLen_ - 2);
  isDir = r.u8() != 0;
  size = r.u32();
  return last_ = r.ok() ? kErrOk : kErrBadArg;
}

int16_t FsClientCore::space(uint32_t& totalMb, uint32_t& freeMb) {
  Writer w(req_, kMaxPayload);
  w.str("/");
  w.u8(kFsStatSpace);
  Reader r(nullptr, 0);
  const int16_t e = call(Msg::FsStat, w.size(), r);
  if (e != kErrOk) return e;
  r.u8();
  r.u32();
  totalMb = r.u32();
  freeMb = r.u32();
  return last_ = r.ok() ? kErrOk : kErrBadArg;
}

int16_t FsClientCore::list(const char* dir, uint16_t start, uint8_t max, FsListPage& page) {
  page.len_ = page.pos_ = page.count_ = page.read_ = 0;
  page.more_ = false;
  if (strlen(dir) >= static_cast<size_t>(kFsPathMax)) return last_ = kErrBadArg;
  Writer w(req_, kMaxPayload);
  w.u16(start);
  w.u8(max);
  w.str(dir);
  Reader r(nullptr, 0);
  const int16_t e = call(Msg::FsList, w.size(), r);
  if (e != kErrOk) return e;
  page.more_ = r.u8() != 0;
  page.count_ = r.u8();
  if (!r.ok()) return last_ = kErrBadArg;
  memcpy(page.buf_, reply_, replyLen_);
  page.len_ = replyLen_;
  page.pos_ = replyLen_ - r.left();
  return kErrOk;
}

int16_t FsClientCore::remove(const char* path) { return pathOp(Msg::FsRemove, path); }

int16_t FsClientCore::rename(const char* from, const char* to) {
  if (strlen(from) >= static_cast<size_t>(kFsPathMax) || strlen(to) >= static_cast<size_t>(kFsPathMax))
    return last_ = kErrBadArg;
  Writer w(req_, kMaxPayload);
  w.str(from);
  w.str(to);
  Reader r(nullptr, 0);
  return call(Msg::FsRename, w.size(), r);
}

int16_t FsClientCore::mkdir(const char* path) { return pathOp(Msg::FsMkdir, path); }

int16_t FsClientCore::rmdir(const char* path) { return pathOp(Msg::FsRmdir, path); }

}  // namespace mt::link
