#include <FS.h>
#include <sys/stat.h>

#include "link_msg.h"
#include "preset_paths.h"
#include "storage/remote_fs.h"
#include "storage/space_query.h"
namespace {
thread_local int16_t lastDirectoryError = mt::link::kErrOk;
bool below(const fs::disk::path& base, const fs::disk::path& path) {
  auto relative = path.lexically_relative(base);
  return !relative.empty() && *relative.begin() != "..";
}
}  // namespace
namespace fs {
disk::path root() {
  const char* env = getenv("DTRK_DATA");
  return disk::absolute(env && *env ? env : "dtrk-data");
}
disk::path resolve(const char* path) {
  if (!path || !*path) return {};
  auto relative = disk::path(path).relative_path();
  for (const auto& part : relative)
    if (part == "..") return {};
  std::error_code ec;
  auto base = disk::weakly_canonical(root(), ec);
  if (ec) return {};
  auto dest = disk::weakly_canonical(root() / relative, ec);
  if (ec || !below(base, dest)) return {};
  return dest;
}
File::File(disk::path path, const char* mode) : path_(std::move(path)), name_(path_.filename().string()) {
  std::error_code ec;
  if (disk::is_directory(path_, ec)) {
    auto entries = std::make_shared<disk::directory_iterator>(path_, ec);
    lastDirectoryError = ec ? mt::link::kErrIo : mt::link::kErrOk;
    if (!ec) directory_ = entries;
  } else if (auto* f = fopen(path_.c_str(), mode))
    handle_ = {f, fclose};
}
size_t File::size() const {
  struct stat info{};
  return handle_ && !fstat(fileno(handle_.get()), &info) ? info.st_size : 0;
}
disk::path File::next() {
  if (!directory_ || *directory_ == disk::directory_iterator{}) return {};
  auto path = (**directory_).path();
  std::error_code ec;
  directory_->increment(ec);
  if (ec) lastDirectoryError = mt::link::kErrIo;
  return path;
}
String File::getNextFileName(bool* dir) {
  auto p = next();
  if (p.empty()) return {};
  std::error_code ec;
  if (dir) *dir = disk::is_directory(p, ec);
  return p.filename().string();
}
File File::openNextFile() { return File(next(), FILE_READ); }
bool FS::exists(const char* path) {
  std::error_code ec;
  return disk::exists(resolve(path), ec);
}
bool FS::mkdir(const char* path) {
  std::error_code ec;
  disk::create_directories(resolve(path), ec);
  return !ec;
}
bool FS::remove(const char* path) {
  std::error_code ec;
  return disk::remove(resolve(path), ec);
}
bool FS::rmdir(const char* path) {
  auto p = resolve(path);
  if (p.empty() || p == resolve("/") || !p.has_filename()) return false;
  std::error_code ec;
  return disk::remove_all(p, ec) > 0 && !ec;
}
bool FS::rename(const char* from, const char* to) {
  std::error_code ec;
  disk::rename(resolve(from), resolve(to), ec);
  return !ec;
}
}  // namespace fs
namespace storage {
fs::FS& remoteFs() {
  static fs::FS card;
  static const bool init = [] {
    for (auto dir : {"projects", "midi", "samples", "samples/rec", "wavetables", "presets/TUN", "templates",
                     "mt-store", "renders", "tunings", "diag"})
      card.mkdir(dir);
    for (int t = 0; t < int(mt::InstrType::Count); ++t) {
      auto type = static_cast<mt::InstrType>(t);
      if (mt::presetTypeHas(type)) card.mkdir(mt::presetRoot(type));
    }
    return true;
  }();
  (void)init;
  return card;
}
int16_t remoteLastError() {
  std::error_code ec;
  if (!fs::disk::is_directory(fs::root(), ec)) return mt::link::kErrNoCard;
  return lastDirectoryError;
}
bool remoteSpace(uint32_t& total, uint32_t& available) {
  std::error_code ec;
  auto s = fs::disk::space(fs::root(), ec);
  if (ec) return false;
  total = s.capacity >> 20;
  available = s.available >> 20;
  return true;
}
int16_t remoteCopy(const char* src, const char* dst) {
  std::error_code ec;
  auto to = fs::resolve(dst);
  fs::disk::create_directories(to.parent_path(), ec);
  return !ec && fs::disk::copy_file(fs::resolve(src), to, ec) ? mt::link::kErrOk : mt::link::kErrIo;
}
void beginSpaceQuery() {}
SpaceResult spaceQuery() {
  uint32_t t = 0, f = 0;
  auto ok = remoteSpace(t, f);
  return {ok ? SpaceState::Ready : SpaceState::Failed, f};
}
}  // namespace storage
