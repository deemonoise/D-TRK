#pragma once
#include "arp_gen.h"
#include "hw/sdcard.h"
#include "storage.h"

namespace storage {

// User arp patterns: /presets/ARP/NAME.arp, the pattern's text form (arp_gen.h). UI task only.
constexpr const char* kArpDir = "/presets/ARP";
// Names (no extension), sorted; 0 without a card or folder.
int listArps(char (*names)[hw::kNameMax], int max);
Result loadArp(const char* name, mt::ArpPattern& out);
// name: validName. Creates the folder; writes NAME.tmp, then replaces NAME.arp.
Result saveArp(const char* name, const mt::ArpPattern& p);

}  // namespace storage
