#include "file_rules.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "preset_paths.h"

namespace mt {
namespace {

// Extension (with the dot) or "" when there is none.
const char* extOf(const char* name) {
  const char* dot = strrchr(name, '.');
  return dot ? dot : "";
}

bool safeChars(const char* name) {
  const size_t n = strlen(name);
  if (n == 0 || n > static_cast<size_t>(kWebNameMax) || name[0] == '.' || strstr(name, "..")) return false;
  for (const char* p = name; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c > 0x7E || strchr("/\\:*?\"<>|", c)) return false;
  }
  return true;
}

}  // namespace

WebDir parseWebDir(const char* s) {
  if (!s) return WebDir::Invalid;
  if (strcmp(s, "midi") == 0) return WebDir::Midi;
  if (strcmp(s, "projects") == 0) return WebDir::Projects;
  if (strcmp(s, "samples") == 0) return WebDir::Samples;
  if (strcmp(s, "presets") == 0) return WebDir::Presets;
  if (strcmp(s, "wavetables") == 0) return WebDir::Wavetables;
  return WebDir::Invalid;
}

const char* webDirPath(WebDir d) {
  switch (d) {
    case WebDir::Midi: return "/midi";
    case WebDir::Projects: return "/projects";
    case WebDir::Samples: return "/samples";
    case WebDir::Presets: return "/presets";
    case WebDir::Wavetables: return "/wavetables";
    default: return "";
  }
}

bool projectBaseValid(const char* base) {
  const size_t n = strlen(base);
  if (n == 0 || n > 16) return false;
  for (const char* p = base; *p; ++p) {
    const char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}

bool webFileAllowed(WebDir d, const char* name) {
  if (!name || !safeChars(name)) return false;
  const char* ext = extOf(name);
  const size_t base = static_cast<size_t>(ext - name);
  if (!ext[0] || base == 0) return false;
  switch (d) {
    case WebDir::Projects: {
      if (strcmp(ext, ".mtp") != 0 && strcmp(ext, ".bak") != 0) return false;
      char b[17];
      if (base > 16) return false;
      memcpy(b, name, base);
      b[base] = 0;
      return projectBaseValid(b);
    }
    case WebDir::Midi: return strcasecmp(ext, ".mid") == 0 && base <= static_cast<size_t>(kMidiBaseMax);
    case WebDir::Samples:
    case WebDir::Wavetables: return strcasecmp(ext, ".wav") == 0;
    case WebDir::Presets: {
      if (strcasecmp(ext, ".mti") != 0 || base > 16) return false;
      char b[17];
      memcpy(b, name, base);
      b[base] = 0;
      return projectBaseValid(b);
    }
    default: return false;
  }
}

uint32_t webMaxBytes(WebDir d) {
  switch (d) {
    case WebDir::Midi: return kMidiMaxBytes;
    case WebDir::Projects: return kProjectMaxBytes;
    case WebDir::Samples: return kWavMaxBytes;
    case WebDir::Presets: return kPresetMaxBytes;
    case WebDir::Wavetables: return kWtMaxBytes;
    default: return 0;
  }
}

bool webRenameAllowed(WebDir d, const char* from, const char* to) {
  return webFileAllowed(d, from) && webFileAllowed(d, to) && strcasecmp(extOf(from), extOf(to)) == 0;
}

bool isOpenProjectFile(const char* project, const char* file) {
  const size_t n = strlen(project);
  return n > 0 && strncasecmp(project, file, n) == 0 && strcasecmp(file + n, ".mtp") == 0;
}

bool webFirmwareName(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  return n > 4 && strcasecmp(name + n - 4, ".bin") == 0;
}

namespace {

bool segChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' ||
         c == '-' || c == '.';
}

// One segment of n chars at s (no '/' inside).
bool segValid(const char* s, size_t n) {
  if (n == 0 || n > static_cast<size_t>(kWebSegMax)) return false;
  if (s[0] == '.' || s[0] == ' ' || s[n - 1] == '.' || s[n - 1] == ' ') return false;  // also "." and ".."
  for (size_t i = 0; i < n; ++i)
    if (!segChar(s[i])) return false;
  return true;
}

// First segment of a /presets subpath (n chars at s): a type folder, as the tracker names it.
bool presetTypeSeg(const char* s, size_t n) {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    if (!presetTypeHas(static_cast<InstrType>(t))) continue;
    const char* name = presetTypeName(static_cast<InstrType>(t));
    if (strlen(name) == n && strncmp(s, name, n) == 0) return true;
  }
  return false;
}

}  // namespace

int webDepthMax(WebDir d) {
  switch (d) {
    case WebDir::Samples:
    case WebDir::Wavetables: return kWebDepthMax;
    case WebDir::Presets: return kPresetDepthMax + 1;  // + the type folder
    default: return 0;
  }
}

bool webSubValid(WebDir d, const char* sub) {
  if (d == WebDir::Invalid || !sub) return false;
  if (!sub[0]) return true;
  if (d == WebDir::Projects) {
    char base[17];
    const char* slash = strchr(sub, '/');
    const size_t n = slash ? static_cast<size_t>(slash - sub) : strlen(sub);
    if (n > 16 || (slash && strcmp(slash, "/wt") != 0)) return false;  // <base> or <base>/wt
    memcpy(base, sub, n);
    base[n] = 0;
    return projectBaseValid(base);
  }
  const int depthMax = webDepthMax(d);
  if (depthMax == 0) return false;
  if (strlen(sub) > static_cast<size_t>(kWebSubMax)) return false;
  int depth = 0;
  for (const char* p = sub;;) {
    const char* slash = strchr(p, '/');
    const size_t n = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (!segValid(p, n) || ++depth > depthMax) return false;
    if (d == WebDir::Presets && depth == 1 && !presetTypeSeg(p, n)) return false;
    if (!slash) return true;
    p = slash + 1;
  }
}

bool webFileAllowedIn(WebDir d, const char* sub, const char* name) {
  if (!sub || !name || !webSubValid(d, sub)) return false;
  if (d == WebDir::Presets && !sub[0]) return false;  // the tracker looks inside type folders only
  if (d != WebDir::Projects || !sub[0]) return webFileAllowed(d, name);
  const char* ext = extOf(name);
  const size_t base = static_cast<size_t>(ext - name);
  if (strcasecmp(ext, ".wav") != 0 || base == 0 || base > 16) return false;
  char b[17];
  memcpy(b, name, base);
  b[base] = 0;
  return projectBaseValid(b);
}

uint32_t webMaxBytesIn(WebDir d, const char* sub) {
  if (!sub || !webSubValid(d, sub)) return 0;
  return d == WebDir::Projects && sub[0] ? kProjWavMaxBytes : webMaxBytes(d);
}

bool webRenameAllowedIn(WebDir d, const char* sub, const char* from, const char* to) {
  return webFileAllowedIn(d, sub, from) && webFileAllowedIn(d, sub, to) && strcasecmp(extOf(from), extOf(to)) == 0;
}

bool webDirListed(WebDir d, const char* sub, const char* name) {
  if (!sub || !name) return false;
  if (d == WebDir::Projects) {
    if (!sub[0]) return projectBaseValid(name);
    return !strchr(sub, '/') && webSubValid(d, sub) && strcmp(name, "wt") == 0;
  }
  return webMkdirAllowed(d, sub, name);
}

bool isOpenProjectFolder(const char* project, WebDir d, const char* sub) {
  return project && project[0] && sub && d == WebDir::Projects && strcasecmp(project, sub) == 0;
}

bool projectFolderOf(const char* file, char out[17]) {
  out[0] = 0;
  if (!file || !webFileAllowed(WebDir::Projects, file)) return false;
  const size_t base = static_cast<size_t>(extOf(file) - file);  // <= 16: checked above
  memcpy(out, file, base);
  out[base] = 0;
  return true;
}

bool projectSubBase(const char* sub, char out[17]) {
  out[0] = 0;
  if (!sub || !sub[0] || !webSubValid(WebDir::Projects, sub)) return false;
  const char* slash = strchr(sub, '/');
  const size_t n = slash ? static_cast<size_t>(slash - sub) : strlen(sub);  // <= 16: checked above
  memcpy(out, sub, n);
  out[n] = 0;
  return true;
}

bool webMkdirAllowed(WebDir d, const char* sub, const char* name) {
  if ((d != WebDir::Samples && d != WebDir::Wavetables && d != WebDir::Presets) || !name || !webSubValid(d, sub) || strchr(name, '/'))
    return false;
  char joined[kWebSubMax + 2];
  const int n = snprintf(joined, sizeof(joined), "%s%s%s", sub, sub[0] ? "/" : "", name);
  return n > 0 && n < static_cast<int>(sizeof(joined)) && webSubValid(d, joined);
}

bool webPath(char* out, size_t cap, WebDir d, const char* sub, const char* name) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (!sub) sub = "";
  if (!name) name = "";
  if (!webSubValid(d, sub)) return false;
  const int n = snprintf(out, cap, "%s%s%s%s%s", webDirPath(d), sub[0] ? "/" : "", sub, name[0] ? "/" : "", name);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = 0;
    return false;
  }
  return true;
}

namespace {

bool jsonAppendRaw(char* buf, size_t cap, size_t& len, const char* name, uint32_t size, bool first, bool dir) {
  size_t i = len;
  if (len >= cap) return false;
  auto put = [&](char c) {
    if (i + 1 >= cap) return false;
    buf[i++] = c;
    return true;
  };
  auto puts = [&](const char* s) {
    for (; *s; ++s)
      if (!put(*s)) return false;
    return true;
  };
  if (!first && !put(',')) return false;
  if (!puts("{\"name\":\"")) return false;
  for (const char* p = name; *p; ++p) {
    if ((*p == '"' || *p == '\\') && !put('\\')) return false;
    if (!put(*p)) return false;
  }
  char tail[24];
  if (dir) strcpy(tail, "\",\"dir\":1}");
  else snprintf(tail, sizeof(tail), "\",\"size\":%lu}", static_cast<unsigned long>(size));
  if (!puts(tail)) return false;
  buf[i] = 0;
  len = i;
  return true;
}

}  // namespace

bool jsonAppendFile(char* buf, size_t cap, size_t& len, const char* name, uint32_t size, bool first) {
  if (jsonAppendRaw(buf, cap, len, name, size, first, false)) return true;
  if (len < cap) buf[len] = 0;  // drop the partial entry
  return false;
}

bool jsonAppendDir(char* buf, size_t cap, size_t& len, const char* name, bool first) {
  if (jsonAppendRaw(buf, cap, len, name, 0, first, true)) return true;
  if (len < cap) buf[len] = 0;
  return false;
}

}  // namespace mt
