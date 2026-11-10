#pragma once

namespace storage {

// Restore an interrupted rotation before opening or replacing a file. UI task only.
// FS operations can fail independently; never discard the only remaining old version.
template <typename FS>
bool recoverFile(FS& fs, const char* dst, const char* backup) {
  return fs.exists(dst) || (fs.exists(backup) && fs.rename(backup, dst));
}

// tmp must already be closed and verified. Keep it on failure so a failed rollback
// still leaves both versions recoverable. A successful commit retains the old dst in backup.
template <typename FS>
bool commitFile(FS& fs, const char* tmp, const char* dst, const char* backup) {
  if (!fs.exists(tmp)) return false;
  if (!fs.exists(dst) && fs.exists(backup) && !fs.rename(backup, dst)) return false;
  const bool had = fs.exists(dst);
  if (had) {
    if (fs.exists(backup) && !fs.remove(backup)) return false;
    if (!fs.rename(dst, backup)) return false;
  }
  if (fs.rename(tmp, dst)) return true;
  if (had) fs.rename(backup, dst);
  return false;
}

}  // namespace storage
