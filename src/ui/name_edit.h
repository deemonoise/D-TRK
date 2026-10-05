#pragma once
#include <string.h>

namespace ui {

// Name editing shared by TRACK and INST: delta steps the character at pos through kNameChars;
// the stored name stays trimmed (no trailing spaces). name holds maxLen chars + the terminator.
constexpr char kNameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ";
constexpr int kNameCharCount = sizeof(kNameChars) - 1;

inline int nameCharIndex(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (int i = 0; i < kNameCharCount; ++i)
    if (kNameChars[i] == c) return i;
  return kNameCharCount - 1;  // unknown or end of string = space
}

inline void editNameChar(char* name, int maxLen, int pos, int delta) {
  char buf[32];
  if (maxLen > static_cast<int>(sizeof(buf)) - 1) maxLen = sizeof(buf) - 1;
  if (pos < 0 || pos >= maxLen) return;
  memset(buf, ' ', maxLen);
  buf[maxLen] = 0;
  const size_t len = strnlen(name, maxLen);
  memcpy(buf, name, len);
  const int i = ((nameCharIndex(buf[pos]) + delta) % kNameCharCount + kNameCharCount) % kNameCharCount;
  buf[pos] = kNameChars[i];
  int end = maxLen;
  while (end > 0 && buf[end - 1] == ' ') --end;
  buf[end] = 0;
  memcpy(name, buf, end + 1);
}

}  // namespace ui
