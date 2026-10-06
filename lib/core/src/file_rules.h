#pragma once
#include <stddef.h>
#include <stdint.h>
#include "wt_file.h"

namespace mt {

// Rules for the Wi-Fi file page: which folders and names are reachable, size limits, JSON listing.

enum class WebDir : uint8_t { Invalid, Midi, Projects, Samples, Presets, Wavetables };

WebDir parseWebDir(const char* s);  // "midi" / "projects" / "samples" / "presets" / "wavetables"
// "/midi", "/projects", "/samples", "/presets", "/wavetables", "" for Invalid
const char* webDirPath(WebDir d);

constexpr int kWebNameMax = 63;    // whole file name incl. extension
constexpr int kMidiBaseMax = kWebNameMax - 4;  // base name without .mid
constexpr uint32_t kProjectMaxBytes = 512 * 1024;
constexpr uint32_t kMidiMaxBytes = 512 * 1024;  // ImportDialog::kMaxFileSize
constexpr uint32_t kWavMaxBytes = 4 * 1024 * 1024;  // samples for the bank (FILE -> SAMPLES)
// WAV in a project folder: any bank sample as Save writes it (the 10.1 MB "samples" partition holds at
// most ~10.1 MB of data, plus the header), so a project downloaded from the page uploads back.
constexpr uint32_t kProjWavMaxBytes = 10 * 1024 * 1024;
constexpr uint32_t kPresetMaxBytes = 1024;  // a .mti is kPresetSize (well under it)
// Wavetable WAV for import (/wavetables): the longest table the importer reads (kWtMaxSrcSamples frames)
// in its widest format (2 channels x 24 bit), plus room for the header chunks.
constexpr uint32_t kWtMaxBytes = kWtMaxSrcSamples * 6 + 64 * 1024;

// 1..16 chars of [A-Za-z0-9_-]: names that load back under themselves.
bool projectBaseValid(const char* base);

// projects: <valid base>.mtp / .bak; midi: <base>.mid, samples: <base>.wav (any case), printable ASCII,
// no path characters (wavetables: <base>.wav as samples); presets: <valid base>.mti (any case of .mti, base as projectBaseValid).
bool webFileAllowed(WebDir d, const char* name);
uint32_t webMaxBytes(WebDir d);
// Both names allowed and the extension stays the same.
bool webRenameAllowed(WebDir d, const char* from, const char* to);
// True when file name is <project>.mtp (FAT names ignore case).
bool isOpenProjectFile(const char* project, const char* file);
// Firmware image name: ends with .bin.
bool webFirmwareName(const char* name);

// Subfolders: /samples, /wavetables and /presets have a tree, /projects one level of project folders
// (/projects/<base>/ holds that project's samples, /projects/<base>/wt its wavetable sources), /midi
// stays flat. For a tree a subpath is relative
// to the section folder, "" = the folder itself, segments joined by '/': each 1..kWebSegMax chars of
// [A-Za-z0-9 _-.], not starting with '.' or ' ', not ending with '.' or ' ' (FAT drops those), at most
// webDepthMax(d) segments, kWebSubMax chars in total. No leading / trailing / doubled '/', no "." / "..",
// no backslashes. /presets: the first segment is a type folder (presetTypeName: CHIP, SAMPLE, FM, DRUM, SYNTH),
// then up to kPresetDepthMax folders as on the tracker.
constexpr int kWebSegMax = 32;
constexpr int kWebDepthMax = 4;
constexpr int kWebSubMax = 100;
// Most segments of a tree's subpath: kWebDepthMax for Samples and Wavetables, kPresetDepthMax + 1 for
// Presets, 0 else.
int webDepthMax(WebDir d);
// "" always (for a valid d); Samples, Wavetables, Presets: a tree as above; Projects: one projectBaseValid
// segment, optionally followed by "/wt" (exactly, as the tracker names it).
bool webSubValid(WebDir d, const char* sub);
// File name allowed in folder sub of d: a project folder (and its wt folder) holds <base>.wav files (base as
// projectBaseValid, any case of .wav); /presets holds .mti only inside a type folder (not at its top);
// elsewhere webFileAllowed(d, name).
bool webFileAllowedIn(WebDir d, const char* sub, const char* name);
// webMaxBytes for a file in folder sub of d: samples of a project folder use the WAV limit. 0 for
// an invalid sub.
uint32_t webMaxBytesIn(WebDir d, const char* sub);
// webRenameAllowed for files in folder sub of d (webFileAllowedIn for both names).
bool webRenameAllowedIn(WebDir d, const char* sub, const char* from, const char* to);
// Folder name shown in a listing of sub (it can be opened again through sub): Samples, Presets as
// webMkdirAllowed, Projects: project folders at the top, "wt" inside a project folder.
bool webDirListed(WebDir d, const char* sub, const char* name);
// True when sub of d is the sample folder of the open project (FAT names ignore case).
bool isOpenProjectFolder(const char* project, WebDir d, const char* sub);
// Sample folder of a project file: <base>.mtp / .bak -> base. False for other names.
bool projectFolderOf(const char* file, char out[17]);
// Project of a /projects subpath: "<base>" or "<base>/wt" -> base. False for "" and invalid subpaths.
bool projectSubBase(const char* sub, char out[17]);
// One folder name (a single segment) that may be created inside sub: depth and length stay in
// limits. Samples, Wavetables and Presets only (a project folder appears with its first upload).
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
