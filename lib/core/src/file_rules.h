#pragma once
#include <stddef.h>
#include <stdint.h>

namespace mt {

// Rules for the Wi-Fi file page: which folders and names are reachable, size limits, JSON listing.

enum class WebDir : uint8_t { Invalid, Midi, Projects, Samples };

WebDir parseWebDir(const char* s);  // "midi" / "projects" / "samples"
const char* webDirPath(WebDir d);   // "/midi", "/projects", "/samples", "" for Invalid

constexpr int kWebNameMax = 63;    // whole file name incl. extension
constexpr int kMidiBaseMax = kWebNameMax - 4;  // base name without .mid
constexpr uint32_t kProjectMaxBytes = 256 * 1024;
constexpr uint32_t kMidiMaxBytes = 512 * 1024;  // ImportDialog::kMaxFileSize
constexpr uint32_t kWavMaxBytes = 4 * 1024 * 1024;  // samples for the bank (FILE -> SAMPLES)

// 1..16 chars of [A-Za-z0-9_-]: names that load back under themselves.
bool projectBaseValid(const char* base);

// projects: <valid base>.mtp / .bak; midi: <base>.mid, samples: <base>.wav (any case), printable ASCII,
// no path characters.
bool webFileAllowed(WebDir d, const char* name);
uint32_t webMaxBytes(WebDir d);
// Both names allowed and the extension stays the same.
bool webRenameAllowed(WebDir d, const char* from, const char* to);
// True when file name is <project>.mtp (FAT names ignore case).
bool isOpenProjectFile(const char* project, const char* file);
// Firmware image name: ends with .bin.
bool webFirmwareName(const char* name);

// Subfolders of /samples (MIDI and projects stay flat). A subpath is relative to the section folder,
// "" = the folder itself, segments joined by '/': each 1..kWebSegMax chars of [A-Za-z0-9 _-.], not
// starting with '.' or ' ', not ending with '.' or ' ' (FAT drops those), at most kWebDepthMax segments,
// kWebSubMax chars in total. No leading / trailing / doubled '/', no "." / "..", no backslashes.
constexpr int kWebSegMax = 32;
constexpr int kWebDepthMax = 4;
constexpr int kWebSubMax = 100;
bool webSubValid(WebDir d, const char* sub);  // "" always (for a valid d); anything else only for Samples
// One folder name (a single segment) that may be created inside sub: depth and length stay in limits.
bool webMkdirAllowed(WebDir d, const char* sub, const char* name);
// "<section>/<sub>/<name>" (sub and name may be ""), or false: invalid sub or the buffer is too small.
// name is not checked here (webFileAllowed / webMkdirAllowed do that).
bool webPath(char* out, size_t cap, WebDir d, const char* sub, const char* name);
constexpr int kWebPathMax = 9 + kWebSubMax + 1 + kWebNameMax + 1;  // "/projects/" + sub + "/" + name + 0

// Appends {"name":"..","size":N} (with a leading comma unless first) to buf[len..cap).
// Keeps buf null-terminated. False (len unchanged, partial entry dropped) when it does not fit.
bool jsonAppendFile(char* buf, size_t cap, size_t& len, const char* name, uint32_t size, bool first);
// Same for a folder: {"name":"..","dir":1}.
bool jsonAppendDir(char* buf, size_t cap, size_t& len, const char* name, bool first);

}  // namespace mt
