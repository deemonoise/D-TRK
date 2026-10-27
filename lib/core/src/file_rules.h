#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mt {

// 1..16 chars of [A-Za-z0-9_-]: names that load back under themselves.
bool projectBaseValid(const char* base);

// Wi-Fi firmware page. Firmware image name: ends with .bin.
bool webFirmwareName(const char* name);
// Synth board firmware (Intel HEX): ends with .hex.
bool webSynthFirmwareName(const char* name);

// Wi-Fi export download: a path under /export/ (a folder or file below it), no "..", "." or empty
// segments, no backslashes.
bool webExportPath(const char* path);
// HTTP Range "bytes=N-" (or "bytes=N-M": served from N to the end): N. Other forms: false.
bool parseRangeStart(const char* range, uint32_t& at);

// A folder browser's B / Back: dir one level up towards root ("/samples/drums/909" -> "/samples/drums",
// left = "909", the folder to put the cursor on). At root (or not below it): false, dir unchanged.
// A dir outside root's tree: root, left empty.
bool browseUp(char* dir, const char* root, char* left, size_t leftCap);

// ---- Browsers (FILE Load / Import MIDI / Import WAV, presets, WT import): one behaviour. ----
// Rows are 0..n-1 (row 0 is the browser's Back / Up row).

// Encoder turn: round and round. n == 0: 0.
int listMove(int sel, int delta, int n);
// Shift + turn: a page (rows) per detent, stopping at the ends; from an end, over to the other one.
int listPage(int sel, int delta, int rows, int n);
// First visible row so sel is on screen (and no empty rows below the list's end).
int listKeepVisible(int sel, int top, int rows, int n);
// Drag: the first visible row kept in 0..n-rows.
int listClampTop(int top, int rows, int n);
// name cut to max chars (and cap - 1), "~" as its last char when cut.
void fitName(const char* name, int max, char* out, size_t cap);
// The end of path in max chars: "..." + tail when cut (max <= 3: the last max chars).
void fitTail(const char* path, int max, char* out, size_t cap);

// Folders of dir below root: 0 at root, -1 when dir is not root or below it.
int browseDepth(const char* dir, const char* root);
enum class BrowseResult : uint8_t { Ok, TooDeep, TooLong };
// Into subfolder sub of dir (dir += "/" + sub): TooDeep when dir is already depthMax folders below
// root, TooLong when it would not fit cap; dir is unchanged then.
BrowseResult browseInto(char* dir, size_t cap, const char* root, const char* sub, int depthMax);

}  // namespace mt
