#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "midi_devices.h"

using namespace mt;

namespace {

MidiDevices d;
uint8_t off = 0;

const char* kA = "aa:aa:aa:aa:aa:01";
const char* kB = "bb:bb:bb:bb:bb:02";
const char* kC = "cc:cc:cc:cc:cc:03";

int add(const char* addr, DevRole role, const char* name = "dev") { return d.add(addr, 0, name, role, off); }

}  // namespace

void setUp() {
  d.clear();
  off = 0xAA;
}
void tearDown() {}

void test_add_count_find() {
  TEST_ASSERT_EQUAL(0, d.count());
  TEST_ASSERT_EQUAL(0, d.add(kA, 1, "SMC-PAD", DevRole::In, off));
  TEST_ASSERT_EQUAL(0, off);
  TEST_ASSERT_EQUAL(1, add(kB, DevRole::Out));
  TEST_ASSERT_EQUAL(2, d.count());
  TEST_ASSERT_EQUAL(0, d.find(kA));
  TEST_ASSERT_EQUAL(1, d.find(kB));
  TEST_ASSERT_EQUAL(-1, d.find(kC));
  TEST_ASSERT_EQUAL(-1, d.find(nullptr));
  const MidiDevice& a = d.at(0);
  TEST_ASSERT_EQUAL_STRING(kA, a.addr);
  TEST_ASSERT_EQUAL(1, a.addrType);
  TEST_ASSERT_EQUAL_STRING("SMC-PAD", a.name);
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::In), static_cast<int>(a.role));
  TEST_ASSERT_TRUE(a.on);
  for (int m = 0; m < static_cast<int>(PadMode::Count); ++m) TEST_ASSERT_EQUAL(0xFF, a.pads.base[m]);
}

void test_add_refuses_duplicate_and_empty() {
  TEST_ASSERT_EQUAL(0, add(kA, DevRole::In));
  TEST_ASSERT_EQUAL(-1, add(kA, DevRole::Out));
  TEST_ASSERT_EQUAL(-1, add("", DevRole::Out));
  TEST_ASSERT_EQUAL(-1, add(nullptr, DevRole::Out));
  TEST_ASSERT_EQUAL(1, d.count());
}

void test_add_seventh_refused() {
  char addr[18];
  for (int i = 0; i < kDevMax; ++i) {
    snprintf(addr, sizeof addr, "00:00:00:00:00:%02d", i);
    TEST_ASSERT_EQUAL(i, add(addr, DevRole::In));
  }
  TEST_ASSERT_EQUAL(-1, add(kA, DevRole::In));
  TEST_ASSERT_EQUAL(kDevMax, d.count());
}

void test_remove_compacts() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  add(kC, DevRole::Out);
  d.remove(0);
  TEST_ASSERT_EQUAL(2, d.count());
  TEST_ASSERT_EQUAL_STRING(kB, d.at(0).addr);
  TEST_ASSERT_EQUAL_STRING(kC, d.at(1).addr);
  d.remove(5);  // out of range
  d.remove(-1);
  TEST_ASSERT_EQUAL(2, d.count());
}

void test_second_in_add_turns_first_off() {
  add(kA, DevRole::In);
  TEST_ASSERT_EQUAL(1, add(kB, DevRole::In));
  TEST_ASSERT_EQUAL(1 << 0, off);
  TEST_ASSERT_FALSE(d.at(0).on);
  TEST_ASSERT_TRUE(d.at(1).on);
  // Turning the first back on turns the second off.
  d.setOn(0, true, off);
  TEST_ASSERT_EQUAL(1 << 1, off);
  TEST_ASSERT_TRUE(d.at(0).on);
  TEST_ASSERT_FALSE(d.at(1).on);
  // Turning off reports nothing.
  d.setOn(0, false, off);
  TEST_ASSERT_EQUAL(0, off);
  TEST_ASSERT_FALSE(d.at(0).on);
}

void test_in_and_out_coexist() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  TEST_ASSERT_EQUAL(0, off);
  TEST_ASSERT_TRUE(d.at(0).on);
  TEST_ASSERT_TRUE(d.at(1).on);
}

void test_both_clashes_with_in_and_out() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  TEST_ASSERT_EQUAL(2, add(kC, DevRole::Both));
  TEST_ASSERT_EQUAL((1 << 0) | (1 << 1), off);
  TEST_ASSERT_FALSE(d.at(0).on);
  TEST_ASSERT_FALSE(d.at(1).on);
  TEST_ASSERT_TRUE(d.at(2).on);
  // An In device back on turns the Both device off.
  d.setOn(0, true, off);
  TEST_ASSERT_EQUAL(1 << 2, off);
  TEST_ASSERT_FALSE(d.at(2).on);
}

void test_role_change_clash() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  d.setRole(1, DevRole::In, off);
  TEST_ASSERT_EQUAL(1 << 0, off);
  TEST_ASSERT_FALSE(d.at(0).on);
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::In), static_cast<int>(d.at(1).role));
  // Role change of an off device clashes with nothing.
  d.setRole(0, DevRole::Both, off);
  TEST_ASSERT_EQUAL(0, off);
  TEST_ASSERT_FALSE(d.at(0).on);
  TEST_ASSERT_TRUE(d.at(1).on);
}

void test_active_in_out() {
  TEST_ASSERT_EQUAL(-1, d.activeIn());
  TEST_ASSERT_EQUAL(-1, d.activeOut());
  add(kA, DevRole::Out);
  add(kB, DevRole::In);
  TEST_ASSERT_EQUAL(1, d.activeIn());
  TEST_ASSERT_EQUAL(0, d.activeOut());
  add(kC, DevRole::Both);
  TEST_ASSERT_EQUAL(2, d.activeIn());
  TEST_ASSERT_EQUAL(2, d.activeOut());
  d.setOn(2, false, off);
  TEST_ASSERT_EQUAL(-1, d.activeIn());
  TEST_ASSERT_EQUAL(-1, d.activeOut());
}

void test_name_truncation() {
  add(kA, DevRole::In, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  TEST_ASSERT_EQUAL(kDevName - 1, static_cast<int>(strlen(d.at(0).name)));
  TEST_ASSERT_EQUAL_STRING("0123456789ABCDEFGHIJKLM", d.at(0).name);
  add(kB, DevRole::Out, nullptr);
  TEST_ASSERT_EQUAL_STRING("", d.at(1).name);
  add("aa:bb:cc:dd:ee:ff:99:88", DevRole::Out);
  TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", d.at(2).addr);
}

void test_blob_round_trip() {
  d.add(kA, 1, "PAD", DevRole::In, off);
  d.add(kB, 0, "FM-1 synth", DevRole::Out, off);
  d.add(kC, 2, "BOTH", DevRole::Both, off);  // turns both others off
  d.at(0).pads.ch[1] = 9;
  d.at(0).pads.base[1] = 36;
  d.at(2).pads.ch[3] = 15;
  d.at(2).pads.base[3] = 112;
  uint8_t buf[MidiDevices::kBlobMax];
  const int n = d.save(buf, sizeof buf);
  TEST_ASSERT_EQUAL(MidiDevices::kBlobHead + 3 * MidiDevices::kDevRec, n);
  TEST_ASSERT_EQUAL(2, buf[0]);
  TEST_ASSERT_EQUAL(3, buf[1]);
  TEST_ASSERT_EQUAL(0, d.save(buf, n - 1));

  MidiDevices e;
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_EQUAL(3, e.count());
  for (int i = 0; i < 3; ++i) {
    const MidiDevice &x = d.at(i), &y = e.at(i);
    TEST_ASSERT_EQUAL_STRING(x.addr, y.addr);
    TEST_ASSERT_EQUAL(x.addrType, y.addrType);
    TEST_ASSERT_EQUAL_STRING(x.name, y.name);
    TEST_ASSERT_EQUAL(static_cast<int>(x.role), static_cast<int>(y.role));
    TEST_ASSERT_EQUAL(x.on, y.on);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(x.pads.ch, y.pads.ch, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(x.pads.base, y.pads.base, 4);
  }
  // Empty list round trip.
  MidiDevices f;
  TEST_ASSERT_EQUAL(2, f.save(buf, sizeof buf));
  TEST_ASSERT_TRUE(e.load(buf, 2));
  TEST_ASSERT_EQUAL(0, e.count());
}

void test_blob_bad_rejected() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  uint8_t buf[MidiDevices::kBlobMax + 4];
  const int n = d.save(buf, sizeof buf);
  MidiDevices e;
  // bad version
  buf[0] = 3;
  e.add(kC, 0, "x", DevRole::In, off);
  TEST_ASSERT_FALSE(e.load(buf, n));
  TEST_ASSERT_EQUAL(0, e.count());
  buf[0] = 2;
  // short / long
  e.add(kC, 0, "x", DevRole::In, off);
  TEST_ASSERT_FALSE(e.load(buf, n - 1));
  TEST_ASSERT_EQUAL(0, e.count());
  e.add(kC, 0, "x", DevRole::In, off);
  TEST_ASSERT_FALSE(e.load(buf, n + 1));
  TEST_ASSERT_EQUAL(0, e.count());
  TEST_ASSERT_FALSE(e.load(buf, 1));
  TEST_ASSERT_FALSE(e.load(nullptr, 0));
  // bad role (record 1's role byte: offset head + rec + 17 + 1 + 23)
  const int roleAt = MidiDevices::kBlobHead + MidiDevices::kDevRec + 41;
  buf[roleAt] = 0;
  e.add(kC, 0, "x", DevRole::In, off);
  TEST_ASSERT_FALSE(e.load(buf, n));
  TEST_ASSERT_EQUAL(0, e.count());
  buf[roleAt] = 4;
  TEST_ASSERT_FALSE(e.load(buf, n));
  buf[roleAt] = static_cast<uint8_t>(DevRole::Out);
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_EQUAL(2, e.count());
  // count above kDevMax
  uint8_t big[MidiDevices::kBlobHead + (kDevMax + 1) * MidiDevices::kDevRec] = {1, kDevMax + 1};
  TEST_ASSERT_FALSE(e.load(big, sizeof big));
  TEST_ASSERT_EQUAL(0, e.count());
}

void test_load_fixes_two_on_ins() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  add(kC, DevRole::Out);  // turns kB off
  uint8_t buf[MidiDevices::kBlobMax];
  const int n = d.save(buf, sizeof buf);
  // Corrupt: record 1 to role In and on.
  const int rec1 = MidiDevices::kBlobHead + MidiDevices::kDevRec;
  buf[rec1 + 41] = static_cast<uint8_t>(DevRole::In);
  buf[rec1 + 42] = 1;
  MidiDevices e;
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_TRUE(e.at(0).on);
  TEST_ASSERT_FALSE(e.at(1).on);
  TEST_ASSERT_TRUE(e.at(2).on);
  TEST_ASSERT_EQUAL(0, e.activeIn());
  TEST_ASSERT_EQUAL(2, e.activeOut());
}

PadLearn learned() {
  PadLearn p;
  p.ch[0] = 9;
  p.base[0] = 36;
  return p;
}

void test_migrate_in_only() {
  d.migrate(kA, 1, "", 0, learned());
  TEST_ASSERT_EQUAL(1, d.count());
  TEST_ASSERT_EQUAL_STRING(kA, d.at(0).addr);
  TEST_ASSERT_EQUAL_STRING(kA, d.at(0).name);
  TEST_ASSERT_EQUAL(1, d.at(0).addrType);
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::In), static_cast<int>(d.at(0).role));
  TEST_ASSERT_TRUE(d.at(0).on);
  TEST_ASSERT_EQUAL(36, d.at(0).pads.base[0]);
  TEST_ASSERT_EQUAL(9, d.at(0).pads.ch[0]);
}

void test_migrate_out_only() {
  d.migrate(nullptr, 0, kB, 1, learned());
  TEST_ASSERT_EQUAL(1, d.count());
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::Out), static_cast<int>(d.at(0).role));
  TEST_ASSERT_EQUAL(0xFF, d.at(0).pads.base[0]);
}

void test_migrate_same_is_both() {
  d.migrate(kA, 1, kA, 1, learned());
  TEST_ASSERT_EQUAL(1, d.count());
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::Both), static_cast<int>(d.at(0).role));
  TEST_ASSERT_TRUE(d.at(0).on);
  TEST_ASSERT_EQUAL(36, d.at(0).pads.base[0]);
}

void test_migrate_two_devices() {
  d.migrate(kA, 1, kB, 0, learned());
  TEST_ASSERT_EQUAL(2, d.count());
  TEST_ASSERT_EQUAL_STRING(kA, d.at(0).addr);
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::In), static_cast<int>(d.at(0).role));
  TEST_ASSERT_EQUAL(36, d.at(0).pads.base[0]);
  TEST_ASSERT_EQUAL_STRING(kB, d.at(1).addr);
  TEST_ASSERT_EQUAL(0, d.at(1).addrType);
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::Out), static_cast<int>(d.at(1).role));
  TEST_ASSERT_EQUAL(0xFF, d.at(1).pads.base[0]);
  TEST_ASSERT_TRUE(d.at(0).on);
  TEST_ASSERT_TRUE(d.at(1).on);
}

void test_migrate_none_and_non_empty() {
  d.migrate("", 0, nullptr, 0, learned());
  TEST_ASSERT_EQUAL(0, d.count());
  add(kC, DevRole::Out);
  d.migrate(kA, 1, kB, 0, learned());
  TEST_ASSERT_EQUAL(1, d.count());
  TEST_ASSERT_EQUAL_STRING(kC, d.at(0).addr);
}


void test_load_clears_bad_pads() {
  add(kA, DevRole::In);
  d.at(0).pads.ch[0] = 9;
  d.at(0).pads.base[0] = 36;  // good
  d.at(0).pads.ch[1] = 16;    // bad channel
  d.at(0).pads.base[1] = 40;
  d.at(0).pads.ch[2] = 2;
  d.at(0).pads.base[2] = 128;  // bad note
  d.at(0).pads.ch[3] = 3;      // not learned: base 0xFF stays
  uint8_t buf[MidiDevices::kBlobMax];
  const int n = d.save(buf, sizeof buf);
  MidiDevices e;
  TEST_ASSERT_TRUE(e.load(buf, n));
  const PadLearn& p = e.at(0).pads;
  TEST_ASSERT_EQUAL(9, p.ch[0]);
  TEST_ASSERT_EQUAL(36, p.base[0]);
  TEST_ASSERT_EQUAL(0, p.ch[1]);
  TEST_ASSERT_EQUAL(0xFF, p.base[1]);
  TEST_ASSERT_EQUAL(0, p.ch[2]);
  TEST_ASSERT_EQUAL(0xFF, p.base[2]);
  TEST_ASSERT_EQUAL(0xFF, p.base[3]);
}

void test_find_and_migrate_ignore_case() {
  add(kA, DevRole::In);
  TEST_ASSERT_EQUAL(0, d.find("AA:AA:AA:AA:AA:01"));
  TEST_ASSERT_EQUAL(-1, add("AA:aa:AA:aa:AA:01", DevRole::Out));
  d.clear();
  d.migrate("bb:bb:bb:bb:bb:02", 0, "BB:BB:BB:BB:BB:02", 0, learned());
  TEST_ASSERT_EQUAL(1, d.count());
  TEST_ASSERT_EQUAL(static_cast<int>(DevRole::Both), static_cast<int>(d.at(0).role));
}

void test_add_refuses_bad_role() {
  TEST_ASSERT_EQUAL(-1, add(kA, static_cast<DevRole>(0)));
  TEST_ASSERT_EQUAL(-1, add(kA, static_cast<DevRole>(4)));
  TEST_ASSERT_EQUAL(0, d.count());
}

void test_load_drops_empty_and_duplicate_addr() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  add(kC, DevRole::Out);
  add("dd:dd:dd:dd:dd:04", DevRole::In);
  uint8_t buf[MidiDevices::kBlobMax];
  const int n = d.save(buf, sizeof buf);
  const int rec = MidiDevices::kDevRec, head = MidiDevices::kBlobHead;
  memset(buf + head + 1 * rec, 0, 17);                 // record 1: empty address
  memcpy(buf + head + 2 * rec, "AA:AA:AA:AA:AA:01", 17);  // record 2: duplicate of record 0
  MidiDevices e;
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_EQUAL(2, e.count());
  TEST_ASSERT_EQUAL_STRING(kA, e.at(0).addr);
  TEST_ASSERT_EQUAL_STRING("dd:dd:dd:dd:dd:04", e.at(1).addr);
}

void test_kit_base_roundtrip_and_legacy_devices() {
  add(kA, DevRole::In);
  add(kB, DevRole::Out);
  TEST_ASSERT_EQUAL(255, d.at(0).kitMidiBase);
  d.at(0).kitMidiBase = 36;
  d.at(1).kitMidiBase = 112;
  uint8_t buf[MidiDevices::kBlobMax];
  int n = d.save(buf, sizeof buf);
  MidiDevices e;
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_EQUAL(36, e.at(0).kitMidiBase);
  TEST_ASSERT_EQUAL(112, e.at(1).kitMidiBase);
  // Convert each record into the exact old v1 layout, removing the appended byte.
  uint8_t old[MidiDevices::kBlobMax] = {1, 2};
  const int oldRec = MidiDevices::kDevRec - 1;
  for (int i = 0; i < 2; ++i)
    memcpy(old + 2 + i * oldRec, buf + 2 + i * MidiDevices::kDevRec, oldRec);
  TEST_ASSERT_TRUE(e.load(old, 2 + 2 * oldRec));
  TEST_ASSERT_EQUAL(255, e.at(0).kitMidiBase);
  TEST_ASSERT_EQUAL(255, e.at(1).kitMidiBase);
  TEST_ASSERT_EQUAL_STRING(kB, e.at(1).addr);
  buf[2 + MidiDevices::kDevRec - 1] = 113;
  TEST_ASSERT_TRUE(e.load(buf, n));
  TEST_ASSERT_EQUAL(255, e.at(0).kitMidiBase);
  TEST_ASSERT_EQUAL(112, e.at(1).kitMidiBase);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_kit_base_roundtrip_and_legacy_devices);
  RUN_TEST(test_load_clears_bad_pads);
  RUN_TEST(test_find_and_migrate_ignore_case);
  RUN_TEST(test_add_refuses_bad_role);
  RUN_TEST(test_load_drops_empty_and_duplicate_addr);
  RUN_TEST(test_add_count_find);
  RUN_TEST(test_add_refuses_duplicate_and_empty);
  RUN_TEST(test_add_seventh_refused);
  RUN_TEST(test_remove_compacts);
  RUN_TEST(test_second_in_add_turns_first_off);
  RUN_TEST(test_in_and_out_coexist);
  RUN_TEST(test_both_clashes_with_in_and_out);
  RUN_TEST(test_role_change_clash);
  RUN_TEST(test_active_in_out);
  RUN_TEST(test_name_truncation);
  RUN_TEST(test_blob_round_trip);
  RUN_TEST(test_blob_bad_rejected);
  RUN_TEST(test_load_fixes_two_on_ins);
  RUN_TEST(test_migrate_in_only);
  RUN_TEST(test_migrate_out_only);
  RUN_TEST(test_migrate_same_is_both);
  RUN_TEST(test_migrate_two_devices);
  RUN_TEST(test_migrate_none_and_non_empty);
  return UNITY_END();
}
