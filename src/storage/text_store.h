#pragma once
#include "hw/sdcard.h"
#include "storage.h"

namespace storage {

// One text file per name in a folder under /presets: DIR/NAME.EXT (ext with the dot). UI task only.
// Names (no extension), sorted; 0 without a card or folder.
int listTexts(const char* dir, const char* ext, char (*names)[hw::kNameMax], int max);
// Reads at most cap - 1 bytes into out, '\0'-terminated.
Result loadText(const char* dir, const char* name, const char* ext, char* out, int cap);
// name: validName. Creates /presets and the folder; writes NAME.tmp, then replaces NAME.EXT
// (the old file goes through NAME.bak so a failed rename can put it back). Appends a newline.
Result saveText(const char* dir, const char* name, const char* ext, const char* text);

}  // namespace storage
