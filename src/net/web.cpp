#include "web.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "file_rules.h"
#include "hw/sdcard.h"
#include "preset_io.h"
#include "preset_paths.h"
#include "storage/storage.h"
#include "web_page.h"

namespace net {
namespace {

constexpr const char* kTmp = "/~upload.tmp";
constexpr size_t kListCap = 16 * 1024;
constexpr uint32_t kRestartDelayMs = 1000;

WebServer* srv = nullptr;
WebHooks hooks;
OpenFile openFile = OpenFile::Untouched;
uint32_t restartAt = 0;
char busyMsg[32];

// One upload at a time (WebServer serves requests one by one).
struct Upload {
  mt::WebDir dir;
  char sub[mt::kWebSubMax + 1];
  char name[mt::kWebNameMax + 1];
  fs::File f;
  bool started;
  int code;  // 0 while fine
  char err[112];  // UTF-8: Cyrillic takes 2 bytes per letter
} up;

struct Fw {
  bool started;
  int code;
  char err[48];
  uint32_t lastKb;
} fw;

// Device log lines: ASCII only (the screen font has no Cyrillic).
void log(const char* fmt, const char* a, const char* b = "") {
  char line[96];
  snprintf(line, sizeof(line), fmt, a, b);
  if (hooks.log) hooks.log(line);
}

void logFail(const char* name, int code) {
  char c[8];
  snprintf(c, sizeof(c), "%d", code);
  log("! %s: HTTP %s", name, c);
}

void busy(const char* msg) {
  strlcpy(busyMsg, msg, sizeof(busyMsg));
  if (hooks.busy) hooks.busy(busyMsg);
}

const char* project() {
  const char* p = hooks.project ? hooks.project() : "";
  return p ? p : "";
}

void touch(const char* name, OpenFile what) {
  if (mt::isOpenProjectFile(project(), name)) openFile = what;
}

void reply(int code, const char* text) { srv->send(code, "text/plain; charset=utf-8", text); }

bool openFolder(mt::WebDir d, const char* sub) { return mt::isOpenProjectFolder(project(), d, sub); }

// "/projects/<base>" of a project file (.mtp / .bak), or false.
bool projectFolderPath(const char* file, char* base, char* path, size_t cap) {
  return mt::projectFolderOf(file, base) && mt::webPath(path, cap, mt::WebDir::Projects, base, "");
}

bool isFolder(const char* path) {
  fs::File f = hw::sdFs().open(path);
  const bool ok = f && f.isDirectory();
  f.close();
  return ok;
}

// Removes the files of a project folder and its "wt" folder (wavetable sources: files only), then the
// folder. Other subfolders, deeper ones, and files whose path does not fit keep it in place. True when
// the folder is gone. wtDepth: how many levels of "wt" folders may still be entered (1 for a project).
bool removeFolder(const char* path, int wtDepth = 1) {
  char child[mt::kWebPathMax];
  int skip = 0;  // leading entries left in place (path too long, a folder not to enter)
  for (;;) {
    fs::File dir = hw::sdFs().open(path);
    if (!dir || !dir.isDirectory()) return false;
    String entry;
    bool isDir, found = false, childDir = false;
    int seen = 0;
    while (hw::sdNextEntry(dir, entry, isDir)) {
      if (seen++ < skip) continue;
      const int n = snprintf(child, sizeof(child), "%s/%s", path, entry.c_str());
      if (n > 0 && n < static_cast<int>(sizeof(child)) && (!isDir || (wtDepth > 0 && strcasecmp(entry.c_str(), "wt") == 0))) {
        found = true;
        childDir = isDir;
        break;
      }
      ++skip;
    }
    dir.close();  // removing while the folder is open for reading is not safe on FAT
    if (!found) break;
    if (childDir ? !removeFolder(child, wtDepth - 1) : !hw::sdFs().remove(child)) return false;
  }
  return hw::sdFs().rmdir(path);
}

// "/projects/<base><ext>" exists.
bool projectFileExists(const char* base, const char* ext) {
  char name[24], p[mt::kWebPathMax];
  snprintf(name, sizeof(name), "%s%s", base, ext);
  return mt::webPath(p, sizeof(p), mt::WebDir::Projects, "", name) && hw::sdFs().exists(p);
}

// "sub/name" for the device log (sub is validated ASCII).
void shownName(char* out, size_t cap, const char* sub, const char* name) {
  snprintf(out, cap, "%s%s%s", sub, sub[0] ? "/" : "", name);
}

// Common checks: card, section, subfolder (samples, wavetables, presets, project folders and their wt), name. False after replying with an error.
// path gets "<section>/<sub>/<name>" (or the folder itself when nameArg is null).
bool checkArgs(mt::WebDir& d, String& sub, const char* nameArg, String& name, char* path, size_t cap) {
  if (!hw::sdReady()) {
    reply(503, "нет карты");
    return false;
  }
  d = mt::parseWebDir(srv->arg("dir").c_str());
  sub = srv->arg("sub");
  if (d == mt::WebDir::Invalid || !mt::webSubValid(d, sub.c_str())) {
    reply(400, "неверная папка");
    return false;
  }
  if (!nameArg) {
    if (mt::webPath(path, cap, d, sub.c_str(), "")) return true;
    reply(400, "неверная папка");
    return false;
  }
  name = srv->arg(nameArg);
  if (!mt::webFileAllowedIn(d, sub.c_str(), name.c_str())) {
    reply(400, "недопустимое имя");
    return false;
  }
  if (!mt::webPath(path, cap, d, sub.c_str(), name.c_str())) {
    reply(400, "слишком длинный путь");
    return false;
  }
  return true;
}

void handleList() {
  mt::WebDir d;
  String sub, unused;
  char path[mt::kWebPathMax];
  if (!checkArgs(d, sub, nullptr, unused, path, sizeof(path))) return;
  char* buf = static_cast<char*>(heap_caps_malloc(kListCap, MALLOC_CAP_SPIRAM));
  if (!buf) {
    reply(500, "нет памяти");
    return;
  }
  fs::File dir = hw::sdFs().open(path);
  if (!dir || !dir.isDirectory()) {
    heap_caps_free(buf);
    char base[17];
    if (d == mt::WebDir::Projects && mt::projectSubBase(sub.c_str(), base) &&
        (projectFileExists(base, ".mtp") || projectFileExists(base, ".bak"))) {
      srv->send(200, "application/json", "[]");  // no samples / tables yet: the first upload makes the folder
      return;
    }
    if (sub.length() && hw::sdFs().exists(mt::webDirPath(d))) {
      reply(404, "папка не найдена");
      return;
    }
    hw::sdRecover();  // card pulled or changed: remount for the next request
    reply(503, "карта не читается (вынута?)");
    return;
  }
  size_t len = 1;
  buf[0] = '[';
  buf[1] = 0;
  bool first = true;
  String name;
  bool isDir;
  while (hw::sdNextEntry(dir, name, isDir)) {
    if (isDir) {
      // Only folders that can be opened again through sub (valid name, not too deep).
      if (!mt::webDirListed(d, sub.c_str(), name.c_str())) continue;
      if (!mt::jsonAppendDir(buf, kListCap - 1, len, name.c_str(), first)) break;
    } else {
      if (!mt::webFileAllowedIn(d, sub.c_str(), name.c_str())) continue;  // also hides legacy.idx
      char filePath[mt::kWebPathMax];
      if (!mt::webPath(filePath, sizeof(filePath), d, sub.c_str(), name.c_str())) continue;
      fs::File f = hw::sdFs().open(filePath);
      if (!f) continue;
      const uint32_t size = static_cast<uint32_t>(f.size());
      f.close();
      if (!mt::jsonAppendFile(buf, kListCap - 1, len, name.c_str(), size, first)) break;
    }
    first = false;
  }
  buf[len++] = ']';
  buf[len] = 0;
  srv->send(200, "application/json", buf);
  heap_caps_free(buf);
}

void handleFile() {
  mt::WebDir d;
  String sub, name;
  char path[mt::kWebPathMax];
  if (!checkArgs(d, sub, "name", name, path, sizeof(path))) return;
  fs::File f = hw::sdFs().open(path, FILE_READ);
  if (!f) {
    reply(404, "файл не найден");
    return;
  }
  srv->sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  srv->streamFile(f, "application/octet-stream");
  f.close();
}

void uploadFail(int code, const char* msg) {
  if (up.code) return;
  Serial.printf("web: upload %s: %d %s (errno %d)\n", up.name, code, msg, errno);
  up.code = code;
  strlcpy(up.err, msg, sizeof(up.err));
  if (up.f) up.f.close();
  hw::sdFs().remove(kTmp);
  if (code == 507) hw::sdRecover();  // write errors usually mean the card was pulled: remount
}

// Uploaded preset (kTmp) loads and its type matches the type folder at the top of sub. False after
// failing the upload.
bool checkPreset(const char* sub) {
  fs::File f = hw::sdFs().open(kTmp, FILE_READ);
  if (!f) {
    uploadFail(507, "не прочитать временный файл");
    return false;
  }
  hw::FileSource src(f);
  mt::Instrument m;
  const mt::LoadErr e = mt::loadPreset(src, m);
  f.close();
  if (e != mt::LoadErr::Ok) {
    uploadFail(400, "файл пресета повреждён");
    return false;
  }
  const char* type = mt::presetTypeName(m.type);
  const size_t n = strlen(type);
  if (strncmp(sub, type, n) != 0 || (sub[n] && sub[n] != '/')) {  // sub starts with a type folder (webSubValid)
    char msg[sizeof(up.err)];
    snprintf(msg, sizeof(msg), "пресет типа %s: его место в /presets/%s", type, type);
    uploadFail(400, msg);
    return false;
  }
  return true;
}

void handleUploadChunk() {
  HTTPUpload& u = srv->upload();
  switch (u.status) {
    case UPLOAD_FILE_START: {
      up = Upload{};
      up.started = true;
      if (!hw::sdReady()) return uploadFail(503, "нет карты");
      up.dir = mt::parseWebDir(srv->arg("dir").c_str());
      if (up.dir == mt::WebDir::Invalid || srv->arg("sub").length() > mt::kWebSubMax) return uploadFail(400, "неверная папка");
      strlcpy(up.sub, srv->arg("sub").c_str(), sizeof(up.sub));
      if (!mt::webSubValid(up.dir, up.sub)) return uploadFail(400, "неверная папка");
      strlcpy(up.name, u.filename.c_str(), sizeof(up.name));
      if (u.filename.length() > mt::kWebNameMax || !mt::webFileAllowedIn(up.dir, up.sub, up.name))
        return uploadFail(400, up.dir == mt::WebDir::Midi      ? "недопустимое имя (нужен .mid, до 59 символов, латиница)"
                               : up.dir == mt::WebDir::Samples || up.dir == mt::WebDir::Wavetables
                                   ? "недопустимое имя (нужен .wav, до 59 символов, латиница)"
                               : up.dir == mt::WebDir::Presets
                                   ? (up.sub[0] ? "недопустимое имя (нужен .mti, до 16 символов: A-Z 0-9 - _)"
                                                : "пресеты лежат в папках типов: FM, DRUM, SAMPLE, CHIP, SYNTH")
                               : up.sub[0] ? "недопустимое имя (нужен .wav, до 16 символов: A-Z 0-9 - _)"
                                           : "недопустимое имя (.mtp/.bak, до 16 символов)");
      char path[mt::kWebPathMax];
      if (!mt::webPath(path, sizeof(path), up.dir, up.sub, up.name)) return uploadFail(400, "слишком длинный путь");
      if (up.sub[0] && up.dir != mt::WebDir::Projects) {  // a project folder is created at the end
        char dirPath[mt::kWebPathMax];
        mt::webPath(dirPath, sizeof(dirPath), up.dir, up.sub, "");
        if (!isFolder(dirPath)) return uploadFail(404, "папка не найдена");
      }
      if (srv->arg("overwrite") != "1" && hw::sdFs().exists(path)) return uploadFail(409, "файл уже есть");
      hw::sdFs().remove(kTmp);
      up.f = hw::sdFs().open(kTmp, FILE_WRITE);
      if (!up.f) return uploadFail(507, "не открыть временный файл на карте");
      busy("UPLOAD...");
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (up.code || !up.f) return;
      if (u.totalSize + u.currentSize > mt::webMaxBytesIn(up.dir, up.sub)) return uploadFail(413, "файл слишком большой");
      if (up.f.write(u.buf, u.currentSize) != u.currentSize) return uploadFail(507, "карта заполнена или ошибка записи");
      break;
    case UPLOAD_FILE_END: {
      if (up.code || !up.f) return;
      up.f.close();
      char path[mt::kWebPathMax];
      mt::webPath(path, sizeof(path), up.dir, up.sub, up.name);  // checked at the start
      if (up.dir == mt::WebDir::Projects && !up.sub[0]) {
        const storage::Result r = storage::installProject(kTmp, up.name);
        if (r != storage::Result::Ok) {
          char msg[48];
          snprintf(msg, sizeof(msg), "проект не принят: %s", storage::resultText(r));
          return uploadFail(r == storage::Result::WriteFail ? 507 : 422, msg);
        }
      } else {
        if (up.dir == mt::WebDir::Presets && !checkPreset(up.sub)) return;
        // Sample or table of a project: its folder (and the wt folder) appear with the first one.
        char dirs[2][mt::kWebPathMax];
        bool made[2] = {false, false};
        // Folders this upload made, newest first (empty: nothing landed there).
        auto unmake = [&] {
          for (int k = 1; k >= 0; --k)
            if (made[k]) hw::sdFs().rmdir(dirs[k]);
        };
        if (up.dir == mt::WebDir::Projects) {
          char base[17];
          mt::projectSubBase(up.sub, base);  // checked at the start
          mt::webPath(dirs[0], sizeof(dirs[0]), up.dir, base, "");
          mt::webPath(dirs[1], sizeof(dirs[1]), up.dir, up.sub, "");
          const int need = strcmp(base, up.sub) == 0 ? 1 : 2;
          for (int i = 0; i < need; ++i) {
            if (isFolder(dirs[i])) continue;
            if (!hw::sdFs().mkdir(dirs[i])) {
              unmake();
              return uploadFail(507, "не создать папку проекта");
            }
            made[i] = true;
          }
        }
        if (hw::sdFs().exists(path) && !hw::sdFs().remove(path)) return uploadFail(507, "не удалось заменить файл");
        if (!hw::sdFs().rename(kTmp, path)) {
          unmake();
          return uploadFail(507, "не переименовать временный файл");
        }
      }
      touch(up.name, OpenFile::Replaced);
      break;
    }
    case UPLOAD_FILE_ABORTED: uploadFail(400, "загрузка прервана"); break;
  }
}

void handleUploadDone() {
  if (!up.started) {
    reply(400, "нет файла");
    return;
  }
  char shown[mt::kWebSubMax + mt::kWebNameMax + 2];
  shownName(shown, sizeof(shown), up.sub, up.name);
  if (up.code) {
    if (up.code != 409) logFail(up.name[0] ? shown : "upload", up.code);
    reply(up.code, up.err);
  } else {
    log("+ %s", shown);
    reply(200, "OK");
  }
  up.started = false;
}

// After a project file rename: the sample folder follows <old>.mtp, or <old>.bak when there is no
// <old>.mtp (a .bak beside its .mtp leaves the folder to the .mtp). The open project's folder stays:
// the tracker saves into it under the open name. Returns a note for the page when the folder was
// meant to move but did not, else nullptr.
const char* renameFolder(const char* from, const char* to) {
  const size_t n = strlen(from);
  const bool bak = n > 4 && strcmp(from + n - 4, ".bak") == 0;
  char oldBase[17], newBase[17], a[mt::kWebPathMax], b[mt::kWebPathMax];
  if (!projectFolderPath(from, oldBase, a, sizeof(a)) || !projectFolderPath(to, newBase, b, sizeof(b))) return nullptr;
  if (strcasecmp(oldBase, newBase) == 0 || !isFolder(a)) return nullptr;  // FAT: case-only change needs nothing
  if (bak && projectFileExists(oldBase, ".mtp")) return nullptr;
  if (openFolder(mt::WebDir::Projects, oldBase)) {
    log("! %s/ kept (open project)", oldBase);
    return "Переименован. Папка с сэмплами осталась под старым именем: проект открыт на трекере";
  }
  if (hw::sdFs().exists(b)) {
    log("! %s/ kept: %s/ exists", oldBase, newBase);
    return "Переименован. Папка с сэмплами осталась под старым именем: папка с новым именем уже есть";
  }
  if (!hw::sdFs().rename(a, b)) {
    log("! %s/ > %s/ failed", oldBase, newBase);
    return "Переименован. Папку с сэмплами переименовать не удалось";
  }
  log("~ %s/ > %s/", oldBase, newBase);
  return nullptr;
}

void handleRename() {
  mt::WebDir d;
  String sub, from;
  char a[mt::kWebPathMax], b[mt::kWebPathMax];
  if (!checkArgs(d, sub, "from", from, a, sizeof(a))) return;
  const String to = srv->arg("to");
  if (!mt::webRenameAllowedIn(d, sub.c_str(), from.c_str(), to.c_str())) {
    reply(400, "недопустимое имя (расширение менять нельзя)");
    return;
  }
  if (!mt::webPath(b, sizeof(b), d, sub.c_str(), to.c_str())) {
    reply(400, "слишком длинный путь");
    return;
  }
  if (!hw::sdFs().exists(a)) {
    reply(404, "файл не найден");
    return;
  }
  if (strcasecmp(from.c_str(), to.c_str()) != 0 && hw::sdFs().exists(b)) {
    reply(409, "файл с таким именем уже есть");
    return;
  }
  if (!hw::sdFs().rename(a, b)) {
    reply(507, "ошибка записи на карту");
    return;
  }
  touch(from.c_str(), OpenFile::Removed);
  touch(to.c_str(), OpenFile::Replaced);
  char shown[mt::kWebSubMax + mt::kWebNameMax + 2];
  shownName(shown, sizeof(shown), sub.c_str(), from.c_str());
  log("~ %s > %s", shown, to.c_str());
  const char* note = d == mt::WebDir::Projects && !sub[0] ? renameFolder(from.c_str(), to.c_str()) : nullptr;
  reply(200, note ? note : "OK");
}

// After a project file is deleted: once neither <base>.mtp nor <base>.bak is left, its sample folder
// goes too. The open project's folder stays (the next save writes it again). Returns a note for the
// page when the folder could not be removed, else nullptr.
const char* dropFolder(const char* file) {
  char base[17], dir[mt::kWebPathMax];
  if (!projectFolderPath(file, base, dir, sizeof(dir)) || openFolder(mt::WebDir::Projects, base)) return nullptr;
  if (projectFileExists(base, ".mtp") || projectFileExists(base, ".bak") || !isFolder(dir)) return nullptr;
  if (removeFolder(dir)) {
    log("x %s/", base);
    return nullptr;
  }
  log("! %s/ not removed", base);
  return "Удалён. Папку с сэмплами удалить не удалось";
}

void handleDelete() {
  mt::WebDir d;
  String sub, name;
  char path[mt::kWebPathMax];
  if (!checkArgs(d, sub, "name", name, path, sizeof(path))) return;
  if (!hw::sdFs().exists(path)) {
    reply(404, "файл не найден");
    return;
  }
  if (!hw::sdFs().remove(path)) {
    reply(507, "ошибка записи на карту");
    return;
  }
  touch(name.c_str(), OpenFile::Removed);
  char shown[mt::kWebSubMax + mt::kWebNameMax + 2];
  shownName(shown, sizeof(shown), sub.c_str(), name.c_str());
  log("x %s", shown);
  const char* note = d == mt::WebDir::Projects && !sub[0] ? dropFolder(name.c_str()) : nullptr;
  reply(200, note ? note : "OK");
}

// Why a folder name is not accepted in sub of d.
const char* mkdirError(mt::WebDir d, const char* sub) {
  switch (d) {
    case mt::WebDir::Samples:
    case mt::WebDir::Wavetables:
      return "недопустимое имя папки (до 32 символов: латиница, цифры, пробел, . _ -; не глубже 4 уровней)";
    case mt::WebDir::Presets:
      return sub[0] ? "недопустимое имя папки (до 32 символов: латиница, цифры, пробел, . _ -; не глубже 4 уровней в папке типа)"
                    : "в /presets только папки типов: FM, DRUM, SAMPLE, CHIP, SYNTH";
    case mt::WebDir::Projects:
      return "папка проекта (и её wt) появляется сама с первым файлом и удаляется вместе с проектом";
    default: return "папки только в разделах сэмплов, таблиц и пресетов";
  }
}

// Folder name argument for mkdir / rmdir (samples, wavetables and presets: project folders come and go with
// the project). False after replying with an error.
bool folderArgs(mt::WebDir& d, String& sub, String& name, char* path, size_t cap) {
  String unused;
  if (!checkArgs(d, sub, nullptr, unused, path, cap)) return false;
  name = srv->arg("name");
  if (!mt::webMkdirAllowed(d, sub.c_str(), name.c_str())) {
    reply(400, mkdirError(d, sub.c_str()));
    return false;
  }
  if (!mt::webPath(path, cap, d, sub.c_str(), name.c_str())) {
    reply(400, "слишком длинный путь");
    return false;
  }
  return true;
}

void handleMkdir() {
  mt::WebDir d;
  String sub, name;
  char path[mt::kWebPathMax];
  if (!folderArgs(d, sub, name, path, sizeof(path))) return;
  if (hw::sdFs().exists(path)) {
    reply(409, "такое имя уже есть");
    return;
  }
  if (!hw::sdFs().mkdir(path)) {
    hw::sdRecover();
    reply(507, "ошибка записи на карту");
    return;
  }
  char shown[mt::kWebSubMax + mt::kWebNameMax + 2];
  shownName(shown, sizeof(shown), sub.c_str(), name.c_str());
  log("+ %s/", shown);
  reply(200, "OK");
}

void handleRmdir() {
  mt::WebDir d;
  String sub, name;
  char path[mt::kWebPathMax];
  if (!folderArgs(d, sub, name, path, sizeof(path))) return;
  if (d == mt::WebDir::Presets && !sub.length()) {
    reply(400, "папки типов не удаляются");
    return;
  }
  fs::File dir = hw::sdFs().open(path);
  if (!dir || !dir.isDirectory()) {
    reply(404, "папка не найдена");
    return;
  }
  String entry;
  bool isDir;
  const bool empty = !hw::sdNextEntry(dir, entry, isDir);  // hidden files count too: rmdir needs a really empty folder
  dir.close();
  if (!empty) {
    reply(409, "папка не пуста: сначала удалите файлы и папки в ней");
    return;
  }
  if (!hw::sdFs().rmdir(path)) {
    hw::sdRecover();
    reply(507, "ошибка записи на карту");
    return;
  }
  char shown[mt::kWebSubMax + mt::kWebNameMax + 2];
  shownName(shown, sizeof(shown), sub.c_str(), name.c_str());
  log("x %s/", shown);
  reply(200, "OK");
}

void fwFail(int code, const char* msg) {
  if (fw.code) return;
  fw.code = code;
  strlcpy(fw.err, msg, sizeof(fw.err));
  if (Update.isRunning()) Update.abort();
}

void handleFwChunk() {
  HTTPUpload& u = srv->upload();
  switch (u.status) {
    case UPLOAD_FILE_START:
      fw = Fw{};
      fw.started = true;
      if (!mt::webFirmwareName(u.filename.c_str())) return fwFail(400, "нужен файл .bin");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) return fwFail(500, Update.errorString());
      busy("FIRMWARE 0 KB");
      break;
    case UPLOAD_FILE_WRITE: {
      if (fw.code) return;
      if (Update.write(u.buf, u.currentSize) != u.currentSize) return fwFail(500, Update.errorString());
      const uint32_t kb = static_cast<uint32_t>((u.totalSize + u.currentSize) / 1024);
      if (kb - fw.lastKb >= 64) {
        fw.lastKb = kb;
        char msg[32];
        snprintf(msg, sizeof(msg), "FIRMWARE %lu KB", static_cast<unsigned long>(kb));
        busy(msg);
      }
      break;
    }
    case UPLOAD_FILE_END:
      if (fw.code) return;
      if (!Update.end(true)) fwFail(500, Update.errorString());  // checks the image
      break;
    case UPLOAD_FILE_ABORTED: fwFail(400, "загрузка прервана"); break;
  }
}

void handleFwDone() {
  if (!fw.started) {
    reply(400, "нет файла");
    return;
  }
  fw.started = false;
  if (fw.code) {
    logFail("firmware", fw.code);
    reply(fw.code, fw.err);
    return;
  }
  reply(200, "OK");
  log("+ firmware%s", "");
  busy("FIRMWARE OK, RESTART");
  restartAt = millis() + kRestartDelayMs;
  if (!restartAt) restartAt = 1;
}

void beginOta() {
  ArduinoOTA.setHostname("d-trk");  // also starts mDNS: http://d-trk.local
  ArduinoOTA.onStart([] { busy("FIRMWARE..."); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int lastPct = 101;
    const unsigned int pct = total ? done * 100 / total : 0;
    if (pct == lastPct || (pct % 5 && pct != 100)) return;
    lastPct = pct;
    char msg[32];
    snprintf(msg, sizeof(msg), "FIRMWARE %u%%", pct);
    busy(msg);
  });
  ArduinoOTA.onEnd([] { busy("FIRMWARE OK, RESTART"); });
  ArduinoOTA.onError([](ota_error_t e) {
    char msg[32];
    snprintf(msg, sizeof(msg), "! OTA error %d", static_cast<int>(e));
    if (hooks.log) hooks.log(msg);
  });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
}

}  // namespace

bool webBegin(const WebHooks& h) {
  if (srv) return true;
  hooks = h;
  openFile = OpenFile::Untouched;
  restartAt = 0;
  srv = new WebServer(80);
  if (!srv) return false;
  srv->on("/", HTTP_GET, [] { srv->send_P(200, "text/html; charset=utf-8", kWebPage); });
  srv->on("/api/list", HTTP_GET, handleList);
  srv->on("/api/file", HTTP_GET, handleFile);
  srv->on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  srv->on("/api/rename", HTTP_POST, handleRename);
  srv->on("/api/delete", HTTP_POST, handleDelete);
  srv->on("/api/mkdir", HTTP_POST, handleMkdir);
  srv->on("/api/rmdir", HTTP_POST, handleRmdir);
  srv->on("/api/update", HTTP_POST, handleFwDone, handleFwChunk);
  srv->onNotFound([] { reply(404, "нет такой страницы"); });
  srv->begin();
  beginOta();
  return true;
}

void webPoll() {
  if (!srv) return;
  srv->handleClient();
  ArduinoOTA.handle();
  if (restartAt && static_cast<int32_t>(millis() - restartAt) >= 0) ESP.restart();
}

void webEnd() {
  if (!srv) return;
  ArduinoOTA.end();  // stops mDNS too
  srv->stop();
  delete srv;
  srv = nullptr;
  if (up.f) up.f.close();
  if (hw::sdReady()) hw::sdFs().remove(kTmp);
  if (Update.isRunning()) Update.abort();
  hooks = WebHooks{};
}

bool webRunning() { return srv != nullptr; }

OpenFile webOpenFile() { return openFile; }

}  // namespace net
