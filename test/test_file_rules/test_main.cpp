#include <unity.h>
#include <string.h>
#include "file_rules.h"
#include "preset_paths.h"
#include "wt_file.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_dirs() {
  TEST_ASSERT_TRUE(parseWebDir("midi") == WebDir::Midi);
  TEST_ASSERT_TRUE(parseWebDir("projects") == WebDir::Projects);
  TEST_ASSERT_TRUE(parseWebDir("../etc") == WebDir::Invalid);
  TEST_ASSERT_TRUE(parseWebDir("") == WebDir::Invalid);
  TEST_ASSERT_TRUE(parseWebDir(nullptr) == WebDir::Invalid);
  TEST_ASSERT_EQUAL_STRING("/midi", webDirPath(WebDir::Midi));
  TEST_ASSERT_EQUAL_STRING("/projects", webDirPath(WebDir::Projects));
  TEST_ASSERT_EQUAL_STRING("", webDirPath(WebDir::Invalid));
}

void test_project_base() {
  TEST_ASSERT_TRUE(projectBaseValid("song_1-a"));
  TEST_ASSERT_TRUE(projectBaseValid("abcdefghijklmnop"));   // 16
  TEST_ASSERT_FALSE(projectBaseValid("abcdefghijklmnopq"));  // 17
  TEST_ASSERT_FALSE(projectBaseValid(""));
  TEST_ASSERT_FALSE(projectBaseValid("my song"));
  TEST_ASSERT_FALSE(projectBaseValid("a.b"));
}

void test_projects_files() {
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Projects, "song.mtp"));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Projects, "song.bak"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "song.MTP"));  // device lists .mtp only as given
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "song.tmp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "song"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, ".mtp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "my song.mtp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "abcdefghijklmnopq.mtp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "a.mid"));
}

void test_midi_files() {
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Midi, "My Song (v2).mid"));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Midi, "LOUD.MID"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a.midi"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a.mtp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "../x.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a/b.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a\\b.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a:b.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a?.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a..b.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, ".hidden.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "\xD0\xBF.mid"));  // non-ASCII
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "a\tb.mid"));
  char n[40];
  char n2[80];
  memset(n2, 'x', 59);
  strcpy(n2 + 59, ".mid");
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Midi, n2));
  memset(n2, 'x', 60);
  strcpy(n2 + 60, ".mid");
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, n2));
  memset(n, 'x', 31);
  strcpy(n + 31, ".mid");
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Midi, n));
  memset(n, 'x', 32);
  strcpy(n + 32, ".mid");
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Midi, n));
  strcpy(n, "a/b.mid");
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, n));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Invalid, "a.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, nullptr));
}

void test_limits_and_rename() {
  TEST_ASSERT_EQUAL_UINT32(512 * 1024, webMaxBytes(WebDir::Midi));
  TEST_ASSERT_EQUAL_UINT32(512 * 1024, webMaxBytes(WebDir::Projects));
  TEST_ASSERT_EQUAL_UINT32(0, webMaxBytes(WebDir::Invalid));
  TEST_ASSERT_TRUE(webRenameAllowed(WebDir::Projects, "a.mtp", "b.mtp"));
  TEST_ASSERT_FALSE(webRenameAllowed(WebDir::Projects, "a.mtp", "b.bak"));
  TEST_ASSERT_TRUE(webRenameAllowed(WebDir::Midi, "a.MID", "b.mid"));
  TEST_ASSERT_FALSE(webRenameAllowed(WebDir::Midi, "a.mid", "b/c.mid"));
}

void test_open_project_and_firmware() {
  TEST_ASSERT_TRUE(isOpenProjectFile("song", "song.mtp"));
  TEST_ASSERT_TRUE(isOpenProjectFile("song", "SONG.mtp"));
  TEST_ASSERT_FALSE(isOpenProjectFile("song", "song.bak"));
  TEST_ASSERT_FALSE(isOpenProjectFile("song", "song2.mtp"));
  TEST_ASSERT_FALSE(isOpenProjectFile("", ".mtp"));
  TEST_ASSERT_TRUE(webFirmwareName("firmware.bin"));
  TEST_ASSERT_TRUE(webFirmwareName("X.BIN"));
  TEST_ASSERT_FALSE(webFirmwareName(".bin"));
  TEST_ASSERT_FALSE(webFirmwareName("firmware.elf"));
  TEST_ASSERT_FALSE(webFirmwareName(nullptr));
}

void test_json() {
  char buf[64];
  size_t len = 0;
  buf[0] = '[';
  len = 1;
  TEST_ASSERT_TRUE(jsonAppendFile(buf, sizeof(buf), len, "a.mid", 12, true));
  TEST_ASSERT_TRUE(jsonAppendFile(buf, sizeof(buf), len, "q\"\\.mid", 0, false));
  TEST_ASSERT_EQUAL_STRING("[{\"name\":\"a.mid\",\"size\":12},{\"name\":\"q\\\"\\\\.mid\",\"size\":0}", buf);
  TEST_ASSERT_EQUAL(strlen(buf), len);
  const size_t before = len;
  TEST_ASSERT_FALSE(jsonAppendFile(buf, sizeof(buf), len, "long-name.mid", 1, false));
  TEST_ASSERT_EQUAL(before, len);
  buf[len] = 0;
  TEST_ASSERT_EQUAL(before, strlen(buf));
}

void test_samples_dir() {
  TEST_ASSERT_TRUE(parseWebDir("samples") == WebDir::Samples);
  TEST_ASSERT_EQUAL_STRING("/samples", webDirPath(WebDir::Samples));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Samples, "kick.WAV"));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Samples, "Snare 01.wav"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Samples, "kick.mid"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Samples, "kick.wave"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Samples, ".wav"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Samples, "../kick.wav"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Midi, "kick.wav"));
  TEST_ASSERT_EQUAL_UINT32(4u * 1024 * 1024, webMaxBytes(WebDir::Samples));
  TEST_ASSERT_EQUAL_UINT32(kWavMaxBytes, webMaxBytes(WebDir::Samples));
  TEST_ASSERT_TRUE(webRenameAllowed(WebDir::Samples, "kick.wav", "kick2.WAV"));
  TEST_ASSERT_FALSE(webRenameAllowed(WebDir::Samples, "kick.wav", "kick.mid"));
  TEST_ASSERT_FALSE(webRenameAllowed(WebDir::Samples, "kick.wav", "kick"));
}

void test_subpath() {
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, ""));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Midi, ""));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, ""));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Invalid, ""));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, nullptr));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, "drums"));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, "drums/808 kit/v1.2_a-b"));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, "a/b/c/d"));          // depth 4
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a/b/c/d/e"));       // depth 5
  TEST_ASSERT_FALSE(webSubValid(WebDir::Midi, "drums"));              // flat sections
  TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, "drums"));           // a project's sample folder
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, ".."));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "."));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a/../b"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a/./b"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, ".hidden"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "._x"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a."));              // FAT drops a trailing dot
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, " a"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a "));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "/a"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a/"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a//b"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "/"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a\\b"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a:b"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a*"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "\xD0\xBF"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, "a\tb"));
  char seg[64];
  memset(seg, 'x', 32);
  seg[32] = 0;
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, seg));
  seg[32] = 'x';
  seg[33] = 0;
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, seg));
  // Total length: 4 segments of 32 chars = 131 > kWebSubMax.
  char longSub[160];
  int n = 0;
  for (int k = 0; k < 4; ++k) {
    if (k) longSub[n++] = '/';
    memset(longSub + n, 'a' + k, 32);
    n += 32;
  }
  longSub[n] = 0;
  TEST_ASSERT_FALSE(webSubValid(WebDir::Samples, longSub));
  longSub[kWebSubMax] = 0;  // cut to exactly 100 chars: "aaa.../bbb.../ccc.../ddd" (last segment 1 char)
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, longSub));
}

void test_mkdir() {
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Samples, "", "drums"));
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Samples, "a/b/c", "d"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "a/b/c/d", "e"));  // too deep
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "", ""));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "", ".."));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "", "a/b"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "../x", "a"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Samples, "", nullptr));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Midi, "", "drums"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Projects, "", "drums"));
}

void test_path() {
  char p[kWebPathMax];
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Samples, "", "kick.wav"));
  TEST_ASSERT_EQUAL_STRING("/samples/kick.wav", p);
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Samples, "drums/808", "kick.wav"));
  TEST_ASSERT_EQUAL_STRING("/samples/drums/808/kick.wav", p);
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Samples, "drums", ""));
  TEST_ASSERT_EQUAL_STRING("/samples/drums", p);
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Samples, nullptr, nullptr));
  TEST_ASSERT_EQUAL_STRING("/samples", p);
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Midi, "", "a.mid"));
  TEST_ASSERT_EQUAL_STRING("/midi/a.mid", p);
  TEST_ASSERT_FALSE(webPath(p, sizeof(p), WebDir::Midi, "x", "a.mid"));
  TEST_ASSERT_EQUAL_STRING("", p);
  TEST_ASSERT_FALSE(webPath(p, sizeof(p), WebDir::Samples, "../etc", "a.wav"));
  TEST_ASSERT_FALSE(webPath(p, sizeof(p), WebDir::Invalid, "", "a.wav"));
  char small[12];
  TEST_ASSERT_FALSE(webPath(small, sizeof(small), WebDir::Samples, "", "kick.wav"));  // truncated
  TEST_ASSERT_EQUAL_STRING("", small);
  // The longest valid sub + the longest name fit kWebPathMax.
  char sub[kWebSubMax + 1];
  memset(sub, 'a', sizeof(sub));
  for (int i = 32; i < kWebSubMax; i += 33) sub[i] = '/';
  sub[kWebSubMax] = 0;
  TEST_ASSERT_TRUE(webSubValid(WebDir::Samples, sub));
  char name[kWebNameMax + 1];
  memset(name, 'n', kWebNameMax);
  memcpy(name + kWebNameMax - 4, ".wav", 5);
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Projects, "", name));
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Samples, sub, name));
}

void test_project_folders() {
  TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, "song1"));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, "abcdefghijklmnop"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "abcdefghijklmnopq"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "song1/x"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "bad name"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, ".."));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "a.b"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "song1", "kick.wav"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "song1", "KICK.WAV"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", "song1.mtp"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", "my kick.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", "abcdefghijklmnopq.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", ".wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", "../x.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "bad name", "kick.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "", "kick.wav"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "", "song1.mtp"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Samples, "drums", "Kick 01.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Samples, "../x", "Kick 01.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1", nullptr));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Projects, "", "song1"));  // created by an upload
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Projects, "song1", "x"));
  char p[kWebPathMax];
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Projects, "song1", "kick.wav"));
  TEST_ASSERT_EQUAL_STRING("/projects/song1/kick.wav", p);
}

void test_project_folder_of() {
  char b[17];
  TEST_ASSERT_TRUE(projectFolderOf("song1.mtp", b));
  TEST_ASSERT_EQUAL_STRING("song1", b);
  TEST_ASSERT_TRUE(projectFolderOf("abcdefghijklmnop.bak", b));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnop", b);
  TEST_ASSERT_FALSE(projectFolderOf("song1.wav", b));
  TEST_ASSERT_FALSE(projectFolderOf("song1", b));
  TEST_ASSERT_FALSE(projectFolderOf("my song.mtp", b));
  TEST_ASSERT_FALSE(projectFolderOf(".mtp", b));
  TEST_ASSERT_FALSE(projectFolderOf(nullptr, b));
}

void test_json_dir() {
  char buf[64];
  size_t len = 1;
  buf[0] = '[';
  buf[1] = 0;
  TEST_ASSERT_TRUE(jsonAppendDir(buf, sizeof(buf), len, "drums", true));
  TEST_ASSERT_TRUE(jsonAppendFile(buf, sizeof(buf), len, "a.wav", 3, false));
  TEST_ASSERT_EQUAL_STRING("[{\"name\":\"drums\",\"dir\":1},{\"name\":\"a.wav\",\"size\":3}", buf);
  const size_t before = len;
  TEST_ASSERT_FALSE(jsonAppendDir(buf, sizeof(buf), len, "a-very-long-folder-name", false));
  TEST_ASSERT_EQUAL(before, len);
  TEST_ASSERT_EQUAL(before, strlen(buf));
}

void test_project_folder_web() {
  // Listing: project folders at the top of /projects only; samples tree as mkdir allows.
  TEST_ASSERT_TRUE(webDirListed(WebDir::Projects, "", "song1"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Projects, "", "bad name"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Projects, "song1", "sub"));
  TEST_ASSERT_TRUE(webDirListed(WebDir::Samples, "", "Drums 1"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Samples, "a/b/c/d", "e"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Midi, "", "x"));
  // Size: samples in a project folder use the WAV limit.
  TEST_ASSERT_EQUAL_UINT32(kProjectMaxBytes, webMaxBytesIn(WebDir::Projects, ""));
  TEST_ASSERT_EQUAL_UINT32(kProjWavMaxBytes, webMaxBytesIn(WebDir::Projects, "song1"));
  TEST_ASSERT_TRUE(kProjWavMaxBytes > kWavMaxBytes);  // any bank sample, written back by Save
  TEST_ASSERT_EQUAL_UINT32(kWavMaxBytes, webMaxBytesIn(WebDir::Samples, "drums"));
  TEST_ASSERT_EQUAL_UINT32(0, webMaxBytesIn(WebDir::Projects, "bad name"));
  // Rename inside a folder.
  TEST_ASSERT_TRUE(webRenameAllowedIn(WebDir::Projects, "song1", "kick.wav", "kick2.WAV"));
  TEST_ASSERT_FALSE(webRenameAllowedIn(WebDir::Projects, "song1", "kick.wav", "my kick.wav"));
  TEST_ASSERT_FALSE(webRenameAllowedIn(WebDir::Projects, "song1", "a.mtp", "b.mtp"));
  TEST_ASSERT_TRUE(webRenameAllowedIn(WebDir::Projects, "", "a.mtp", "b.mtp"));
  TEST_ASSERT_FALSE(webRenameAllowedIn(WebDir::Projects, "", "a.mtp", "b.bak"));
  TEST_ASSERT_TRUE(webRenameAllowedIn(WebDir::Samples, "drums", "Kick 01.wav", "Kick 02.wav"));
  // The open project's sample folder.
  TEST_ASSERT_TRUE(isOpenProjectFolder("song1", WebDir::Projects, "song1"));
  TEST_ASSERT_TRUE(isOpenProjectFolder("song1", WebDir::Projects, "SONG1"));
  TEST_ASSERT_FALSE(isOpenProjectFolder("song1", WebDir::Projects, ""));
  TEST_ASSERT_FALSE(isOpenProjectFolder("song1", WebDir::Projects, "song12"));
  TEST_ASSERT_FALSE(isOpenProjectFolder("", WebDir::Projects, ""));
  TEST_ASSERT_FALSE(isOpenProjectFolder("song1", WebDir::Samples, "song1"));
  TEST_ASSERT_FALSE(isOpenProjectFolder(nullptr, WebDir::Projects, "song1"));
  // legacy.idx and other service files never show up in /projects.
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "", "legacy.idx"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "", "song1.tmp"));
}

void test_presets_web() {
  TEST_ASSERT_TRUE(parseWebDir("presets") == WebDir::Presets);
  TEST_ASSERT_EQUAL_STRING("/presets", webDirPath(WebDir::Presets));
  TEST_ASSERT_EQUAL_INT(kPresetDepthMax + 1, webDepthMax(WebDir::Presets));
  TEST_ASSERT_EQUAL_INT(kWebDepthMax, webDepthMax(WebDir::Samples));
  TEST_ASSERT_EQUAL_INT(0, webDepthMax(WebDir::Midi));
  // Type folder first, then up to kPresetDepthMax folders.
  TEST_ASSERT_TRUE(webSubValid(WebDir::Presets, ""));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Presets, "CHIP"));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Presets, "DRUM/808 kit/a/b/c"));    // 1 + 4
  TEST_ASSERT_FALSE(webSubValid(WebDir::Presets, "DRUM/808 kit/a/b/c/d"));  // 1 + 5
  TEST_ASSERT_FALSE(webSubValid(WebDir::Presets, "chip"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Presets, "KIT"));  // no KIT presets
  TEST_ASSERT_FALSE(webSubValid(WebDir::Presets, "BASS/x"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Presets, "FM/../CHIP"));
  // .mti only, base as a project name, never at the top.
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Presets, "FM", "EPIANO_1.mti"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Presets, "FM/keys", "bell-2.MTI"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Presets, "", "bell.mti"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Presets, "FM", "bell.tmp"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Presets, "FM", "bell 2.mti"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Presets, "FM", "ABCDEFGHIJKLMNOPQ.mti"));  // 17
  TEST_ASSERT_EQUAL_UINT32(1024u, webMaxBytesIn(WebDir::Presets, "CHIP"));
  TEST_ASSERT_TRUE(webRenameAllowedIn(WebDir::Presets, "CHIP", "a.mti", "b.mti"));
  // Folders: only type folders at the top.
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Presets, "", "SAMPLE"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Presets, "", "bass"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Presets, "", "KIT"));
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Presets, "SAMPLE", "Pads 2"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Presets, "SAMPLE/a/b/c/d", "e"));
  TEST_ASSERT_TRUE(webDirListed(WebDir::Presets, "", "DRUM"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Presets, "", "misc"));
  char p[kWebPathMax];
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Presets, "FM/keys", "bell.mti"));
  TEST_ASSERT_EQUAL_STRING("/presets/FM/keys/bell.mti", p);
}

void test_wavetables_web() {
  TEST_ASSERT_TRUE(parseWebDir("wavetables") == WebDir::Wavetables);
  TEST_ASSERT_EQUAL_STRING("/wavetables", webDirPath(WebDir::Wavetables));
  // .wav only, any case; other extensions and path tricks refused.
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Wavetables, "Basic Shapes.wav"));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Wavetables, "PWM.WAV"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Wavetables, "song.mtp"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Wavetables, "a.mti"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Wavetables, "a.wt"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Wavetables, ".wav"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Wavetables, "../a.wav"));
  TEST_ASSERT_TRUE(webRenameAllowed(WebDir::Wavetables, "a.wav", "b.WAV"));
  TEST_ASSERT_FALSE(webRenameAllowed(WebDir::Wavetables, "a.wav", "a.mtp"));
  // Size: any table the importer reads (kWtMaxSrcSamples frames of up to 2 ch x 24 bit) plus headers.
  TEST_ASSERT_EQUAL_UINT32(kWtMaxBytes, webMaxBytes(WebDir::Wavetables));
  TEST_ASSERT_EQUAL_UINT32(kWtMaxBytes, webMaxBytesIn(WebDir::Wavetables, "Serum/Analog"));
  TEST_ASSERT_TRUE(kWtMaxBytes >= kWtMaxSrcSamples * 6u);
  TEST_ASSERT_TRUE(kWtMaxBytes >= 1024u * 1024);
  // Subfolders as in /samples (the tracker's picker goes 4 levels down).
  TEST_ASSERT_EQUAL_INT(kWebDepthMax, webDepthMax(WebDir::Wavetables));
  TEST_ASSERT_TRUE(webSubValid(WebDir::Wavetables, "a/b/c/d"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Wavetables, "a/b/c/d/e"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Wavetables, "../x"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Wavetables, "", "a.wav"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Wavetables, "Serum", "Saw Sweep.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Wavetables, "Serum", "x.mid"));
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Wavetables, "", "Serum"));
  TEST_ASSERT_TRUE(webMkdirAllowed(WebDir::Wavetables, "a/b/c", "d"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Wavetables, "a/b/c/d", "e"));
  TEST_ASSERT_TRUE(webDirListed(WebDir::Wavetables, "", "Serum"));
  TEST_ASSERT_TRUE(webRenameAllowedIn(WebDir::Wavetables, "Serum", "a.wav", "b.wav"));
  char p[kWebPathMax];
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Wavetables, "Serum", "a.wav"));
  TEST_ASSERT_EQUAL_STRING("/wavetables/Serum/a.wav", p);
  TEST_ASSERT_FALSE(isOpenProjectFolder("song1", WebDir::Wavetables, "song1"));
}

void test_project_wt_folder() {
  // The wavetable sources of a project: /projects/<base>/wt, one level, exact name.
  TEST_ASSERT_TRUE(webSubValid(WebDir::Projects, "song1/wt"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "song1/WT"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "song1/wt/x"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "bad name/wt"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "wt/song1"));
  TEST_ASSERT_FALSE(webSubValid(WebDir::Projects, "song1/wt/"));
  TEST_ASSERT_TRUE(webDirListed(WebDir::Projects, "song1", "wt"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Projects, "song1", "WT"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Projects, "song1", "x"));
  TEST_ASSERT_FALSE(webDirListed(WebDir::Projects, "song1/wt", "wt"));
  TEST_ASSERT_TRUE(webFileAllowedIn(WebDir::Projects, "song1/wt", "SAWSQR.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1/wt", "my table.wav"));
  TEST_ASSERT_FALSE(webFileAllowedIn(WebDir::Projects, "song1/wt", "song1.mtp"));
  TEST_ASSERT_EQUAL_UINT32(kProjWavMaxBytes, webMaxBytesIn(WebDir::Projects, "song1/wt"));
  TEST_ASSERT_FALSE(webMkdirAllowed(WebDir::Projects, "song1", "wt"));  // the tracker or an upload makes it
  TEST_ASSERT_FALSE(isOpenProjectFolder("song1", WebDir::Projects, "song1/wt"));
  char b[17];
  TEST_ASSERT_TRUE(projectSubBase("song1/wt", b));
  TEST_ASSERT_EQUAL_STRING("song1", b);
  TEST_ASSERT_TRUE(projectSubBase("song1", b));
  TEST_ASSERT_EQUAL_STRING("song1", b);
  TEST_ASSERT_FALSE(projectSubBase("", b));
  TEST_ASSERT_FALSE(projectSubBase("bad name/wt", b));
  TEST_ASSERT_FALSE(projectSubBase(nullptr, b));
  char p[kWebPathMax];
  TEST_ASSERT_TRUE(webPath(p, sizeof(p), WebDir::Projects, "song1/wt", "SAWSQR.wav"));
  TEST_ASSERT_EQUAL_STRING("/projects/song1/wt/SAWSQR.wav", p);
}

void test_crash_log_listed_in_projects() {
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Projects, "crashlog.txt"));
  TEST_ASSERT_TRUE(webFileAllowed(WebDir::Projects, "cpuprof.txt"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Projects, "other.txt"));
  TEST_ASSERT_FALSE(webFileAllowed(WebDir::Samples, "crashlog.txt"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dirs);
  RUN_TEST(test_samples_dir);
  RUN_TEST(test_project_base);
  RUN_TEST(test_projects_files);
  RUN_TEST(test_midi_files);
  RUN_TEST(test_limits_and_rename);
  RUN_TEST(test_open_project_and_firmware);
  RUN_TEST(test_json);
  RUN_TEST(test_subpath);
  RUN_TEST(test_mkdir);
  RUN_TEST(test_path);
  RUN_TEST(test_json_dir);
  RUN_TEST(test_project_folders);
  RUN_TEST(test_project_folder_of);
  RUN_TEST(test_project_folder_web);
  RUN_TEST(test_presets_web);
  RUN_TEST(test_wavetables_web);
  RUN_TEST(test_project_wt_folder);
  RUN_TEST(test_crash_log_listed_in_projects);
  return UNITY_END();
}
