#include <stdio.h>
#include <string.h>
#include <type_traits>
#include <unity.h>
#include "bytebeat_compile.h"
#include "bytebeat_lib.h"
#include "bytebeat_vm.h"

using namespace mt;

void setUp() {}
void tearDown() {}

namespace {

ByteCode compileOk(const char* src, uint8_t mode = kByteUnsigned) {
  ByteCode bc;
  ByteCompileResult r = byteCompile(src, mode, bc);
  if (r.err != ByteErr::Ok) {
    char msg[128];
    snprintf(msg, sizeof msg, "'%s': err %d at %d", src, static_cast<int>(r.err), r.pos);
    TEST_FAIL_MESSAGE(msg);
  }
  TEST_ASSERT_TRUE(bc.len > 0);
  TEST_ASSERT_TRUE(byteVerify(bc));
  return bc;
}

template <typename F>
void checkAgainst(const char* src, F ref, int a = 0, int b = 0, int c = 0) {
  ByteCode bc = compileOk(src);
  for (uint32_t t = 0; t < 65536; ++t) {
    uint32_t want = ref(t, static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c));
    int32_t got = byteEval(bc, t, a, b, c);
    if (static_cast<uint32_t>(got) != want) {
      char msg[160];
      snprintf(msg, sizeof msg, "'%s' t=%u want %u got %u", src, static_cast<unsigned>(t),
               static_cast<unsigned>(want), static_cast<unsigned>(got));
      TEST_FAIL_MESSAGE(msg);
    }
  }
}

void checkErr(const char* src, ByteErr err, int pos) {
  ByteCode bc;
  bc.len = 7;
  ByteCompileResult r = byteCompile(src, kByteUnsigned, bc);
  char msg[96];
  snprintf(msg, sizeof msg, "'%s': err %d at %d", src, static_cast<int>(r.err), r.pos);
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(err), static_cast<int>(r.err), msg);
  TEST_ASSERT_EQUAL_MESSAGE(pos, r.pos, msg);
  TEST_ASSERT_EQUAL_MESSAGE(0, bc.len, msg);
}

}  // namespace

void test_bytecode_pod() {
  TEST_ASSERT_TRUE(std::is_trivially_copyable<ByteCode>::value);
  TEST_ASSERT_TRUE(std::is_standard_layout<ByteCode>::value);
  TEST_ASSERT_TRUE(sizeof(ByteCode) <= 64);
}

void test_compile_simple() {
  checkAgainst("t", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t; });
  checkAgainst("t*5&t>>7", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * 5 & t >> 7; });
  checkAgainst("t*((t>>12|t>>8)&63&t>>4)",
               [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * ((t >> 12 | t >> 8) & 63 & t >> 4); });
  checkAgainst("  t * ( 42 & t >> 10 ) ", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * (42 & t >> 10); });
}

void test_compile_ternary() {
  checkAgainst("t>>8&1?t:0", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t >> 8 & 1 ? t : 0u; });
  checkAgainst("t&256?t*2:t&512?t*3:t>>1",
               [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t & 256 ? t * 2 : t & 512 ? t * 3 : t >> 1; });
  checkAgainst("(t&4096?t:t*3)+1", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return (t & 4096 ? t : t * 3) + 1; });
  checkAgainst("t&1?t&2?5:6:7", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t & 1 ? t & 2 ? 5u : 6u : 7u; });
}

void test_compile_unary() {
  checkAgainst("-t", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return 0u - t; });
  checkAgainst("~t>>3", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return ~t >> 3; });
  checkAgainst("!(t&7)*t", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return static_cast<uint32_t>(!(t & 7)) * t; });
  checkAgainst("-~!t+--t", [](uint32_t t, uint32_t, uint32_t, uint32_t) {
    return (0u - ~static_cast<uint32_t>(!t)) + (0u - (0u - t));
  });
  checkAgainst("t*-3", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * (0u - 3u); });
}

void test_compile_hex_and_params() {
  checkAgainst("t*(0xCA98>>(t>>9&14)&15)|t>>8",
               [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * (0xCA98 >> (t >> 9 & 14) & 15) | t >> 8; });
  checkAgainst("t^0xDEADBEEF", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t ^ 0xDEADBEEFu; });
  checkAgainst("t*0X1ff+70000", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t * 0x1ff + 70000; });
  checkAgainst("t*(a&t>>b)+c", [](uint32_t t, uint32_t a, uint32_t b, uint32_t c) { return t * (a & t >> b) + c; },
               42, 10, 3);
  checkAgainst("a*b-c", [](uint32_t, uint32_t a, uint32_t b, uint32_t c) { return a * b - c; }, 127, 0, 5);
}

void test_compile_precedence() {
  checkAgainst("t+t*3-t/5%7", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t + t * 3 - t / 5 % 7; });
  checkAgainst("t<<2+1>>1", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return (t << (2 + 1)) >> 1; });
  checkAgainst("t&255==t|t^7", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return (t & (255 == t)) | (t ^ 7); });
  checkAgainst("t>>4<100==t%3>1", [](uint32_t t, uint32_t, uint32_t, uint32_t) {
    return static_cast<uint32_t>((t >> 4 < 100) == (t % 3 > 1));
  });
  checkAgainst("t<=500&&t>=100||t!=7&&t%5==0", [](uint32_t t, uint32_t, uint32_t, uint32_t) {
    return static_cast<uint32_t>((t <= 500 && t >= 100) || (t != 7 && t % 5 == 0));
  });
  checkAgainst("t-1-2-3", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t - 1 - 2 - 3; });
  checkAgainst("t/3/2", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return t / 3 / 2; });
  checkAgainst("(t>>7|t|t>>6)*10+4*(t&t>>13|t>>6)", [](uint32_t t, uint32_t, uint32_t, uint32_t) {
    return (t >> 7 | t | t >> 6) * 10 + 4 * ((t & t >> 13) | t >> 6);
  });
  checkAgainst("t-100<50", [](uint32_t t, uint32_t, uint32_t, uint32_t) { return static_cast<uint32_t>(t - 100 < 50); });
}

void test_compile_errors() {
  checkErr("", ByteErr::Empty, 0);
  checkErr("   ", ByteErr::Empty, 3);
  checkErr("t+", ByteErr::Syntax, 2);
  checkErr("(t", ByteErr::Unbalanced, 2);
  checkErr("t)", ByteErr::Unbalanced, 1);
  checkErr("t$", ByteErr::BadToken, 1);
  checkErr("t t", ByteErr::Syntax, 2);
  checkErr("x*2", ByteErr::BadToken, 0);
  checkErr("tt", ByteErr::BadToken, 0);
  checkErr("t=1", ByteErr::BadToken, 1);
  checkErr("0x", ByteErr::BadToken, 0);
  checkErr("99999999999", ByteErr::BadToken, 0);
  checkErr("t?1", ByteErr::Syntax, 3);
  checkErr("()", ByteErr::Syntax, 1);
  checkErr("*t", ByteErr::Syntax, 0);
  // 17 operands pending: needs a stack of 17
  checkErr("t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+t)))))))))))))))", ByteErr::TooDeep, 47);
  // 16 is fine
  compileOk("t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+(t+t))))))))))))))");
  // 13 five-byte constants: the 10th overflows 61 bytes
  checkErr("t^99999+99999+99999+99999+99999+99999+99999+99999+99999+99999+99999+99999+99999", ByteErr::TooLong, 62);
  char deep[200];
  memset(deep, '(', 100);
  deep[100] = 't';
  memset(deep + 101, ')', 98);
  deep[199] = 0;
  ByteCode bc;
  ByteCompileResult r = byteCompile(deep, kByteUnsigned, bc);
  TEST_ASSERT_EQUAL(static_cast<int>(ByteErr::TooDeep), static_cast<int>(r.err));
  TEST_ASSERT_EQUAL(0, bc.len);
}

void test_compile_stack_depth_recorded() {
  TEST_ASSERT_EQUAL(1, compileOk("t").depth);
  TEST_ASSERT_EQUAL(2, compileOk("t*5").depth);
  TEST_ASSERT_EQUAL(3, compileOk("t*(t+1)").depth);
  TEST_ASSERT_EQUAL(3, compileOk("t&1?2:3").depth);
}

void test_compile_mode_and_constants() {
  ByteCode bc = compileOk("t", kByteSigned);
  TEST_ASSERT_EQUAL(kByteSigned, bc.mode);
  // 8-bit, 16-bit and 32-bit constants: 1 + 2 + 3 + 5 bytes, + 2 operators
  bc = compileOk("200+60000+70000");
  TEST_ASSERT_EQUAL(2 + 3 + 5 + 2, bc.len);
  TEST_ASSERT_EQUAL(130200, byteEval(bc, 0, 0, 0, 0));
  ByteCode bad;
  TEST_ASSERT_TRUE(byteCompile("t", 2, bad).err == ByteErr::BadMode);
  TEST_ASSERT_EQUAL(0, bad.len);
}

void test_vm_div_mod_zero() {
  ByteCode bc = compileOk("t/(t&1)");
  TEST_ASSERT_EQUAL(0, byteEval(bc, 4, 0, 0, 0));
  TEST_ASSERT_EQUAL(5, byteEval(bc, 5, 0, 0, 0));
  bc = compileOk("t%(t&3)");
  TEST_ASSERT_EQUAL(0, byteEval(bc, 8, 0, 0, 0));
  TEST_ASSERT_EQUAL(1, byteEval(bc, 7, 0, 0, 0));
  TEST_ASSERT_EQUAL(0, byteEval(compileOk("t/0"), 1234, 0, 0, 0));
  TEST_ASSERT_EQUAL(0, byteEval(compileOk("t%0"), 1234, 0, 0, 0));
}

void test_vm_shift_masked() {
  ByteCode a = compileOk("t<<40"), b = compileOk("t<<8");
  ByteCode c = compileOk("t>>33"), d = compileOk("t>>1");
  for (uint32_t t = 0; t < 70000; t += 7) {
    TEST_ASSERT_EQUAL(byteEval(b, t, 0, 0, 0), byteEval(a, t, 0, 0, 0));
    TEST_ASSERT_EQUAL(byteEval(d, t, 0, 0, 0), byteEval(c, t, 0, 0, 0));
  }
  TEST_ASSERT_EQUAL(static_cast<int32_t>(0x7FFFFFFFu), byteEval(compileOk("-1>>1"), 0, 0, 0, 0));  // logical
}

void test_vm_deterministic() {
  ByteCode bc = compileOk("(t*(t>>8|t>>9)&46&t>>8)^(t&t>>13|t>>6)");
  ByteCode copy;
  memcpy(&copy, &bc, sizeof bc);
  for (uint32_t t = 0; t < 100000; t += 13) TEST_ASSERT_EQUAL(byteEval(bc, t, 1, 2, 3), byteEval(copy, t, 1, 2, 3));
}

void test_vm_malformed_is_safe() {
  ByteCode bc;
  memset(&bc, 0, sizeof bc);
  TEST_ASSERT_EQUAL(0, byteEval(bc, 99, 0, 0, 0));  // empty
  TEST_ASSERT_FALSE(byteVerify(bc));
  // garbage: any bytes, too long len, underflowing operators, truncated constants
  uint32_t seed = 1;
  for (int k = 0; k < 2000; ++k) {
    bc.len = static_cast<uint8_t>(seed >> 24);
    bc.mode = 0;
    for (uint8_t& o : bc.ops) {
      seed = seed * 1664525u + 1013904223u;
      o = static_cast<uint8_t>(seed >> 27);  // mostly valid opcodes, random structure
    }
    byteEval(bc, seed, 1, 2, 3);
  }
  ByteCode under = compileOk("t+1");
  under.ops[0] = static_cast<uint8_t>(ByteOp::Add);  // pops past the bottom
  TEST_ASSERT_FALSE(byteVerify(under));
  byteEval(under, 5, 0, 0, 0);
  ByteCode cut = compileOk("t+70000");
  cut.len = 3;  // K32 without its bytes
  TEST_ASSERT_FALSE(byteVerify(cut));
  byteEval(cut, 5, 0, 0, 0);
}

void test_vm_sample_modes() {
  TEST_ASSERT_EQUAL_FLOAT(-1.f, byteSample(0, kByteUnsigned));
  TEST_ASSERT_EQUAL_FLOAT(0.f, byteSample(128, kByteUnsigned));
  TEST_ASSERT_EQUAL_FLOAT(127.f / 128.f, byteSample(255, kByteUnsigned));
  TEST_ASSERT_EQUAL_FLOAT(-1.f, byteSample(256, kByteUnsigned));
  TEST_ASSERT_EQUAL_FLOAT(0.f, byteSample(0, kByteSigned));
  TEST_ASSERT_EQUAL_FLOAT(-1.f, byteSample(128, kByteSigned));
  TEST_ASSERT_EQUAL_FLOAT(-1.f / 128.f, byteSample(255, kByteSigned));
  TEST_ASSERT_EQUAL_FLOAT(127.f / 128.f, byteSample(127, kByteSigned));
  TEST_ASSERT_EQUAL_FLOAT(-1.f / 128.f, byteSample(-1, kByteSigned));
}

void test_lib_count_and_entries() {
  int n = byteFormulaCount();
  TEST_ASSERT_TRUE(n >= 40 && n <= 50);
  for (int i = 0; i < n; ++i) {
    const ByteFormula& f = byteFormula(i);
    TEST_ASSERT_NOT_NULL(f.name);
    TEST_ASSERT_TRUE(strlen(f.name) > 0 && strlen(f.name) <= 8);
    TEST_ASSERT_TRUE(f.mode == kByteUnsigned || f.mode == kByteSigned);
    ByteCode bc;
    ByteCompileResult r = byteCompile(f.expr, f.mode, bc);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ByteErr::Ok), static_cast<int>(r.err), f.name);
    const ByteCode& lib = byteCode(i);
    TEST_ASSERT_TRUE_MESSAGE(byteVerify(lib), f.name);
    TEST_ASSERT_EQUAL_MEMORY(&bc, &lib, sizeof bc);
    TEST_ASSERT_EQUAL(f.mode, lib.mode);
    for (int j = 0; j < i; ++j) TEST_ASSERT_TRUE_MESSAGE(strcmp(f.name, byteFormula(j).name) != 0, f.name);
    TEST_ASSERT_TRUE_MESSAGE(f.a <= 127 && f.b <= 127 && f.c <= 127, f.name);
    TEST_ASSERT_TRUE_MESSAGE(byteUses(lib) != 0, f.name);  // A B C do something on every formula
    // not silent: the output byte changes over the first 2^16 ticks at the defaults
    float lo = 2.f, hi = -2.f;
    for (uint32_t t = 0; t < 65536; t += 3) {
      float x = byteSample(byteEval(lib, t, f.a, f.b, f.c), lib.mode);
      lo = x < lo ? x : lo;
      hi = x > hi ? x : hi;
    }
    TEST_ASSERT_TRUE_MESSAGE(hi - lo > 0.25f, f.name);
  }
  // out of range: clamped to the first entry
  TEST_ASSERT_EQUAL_PTR(&byteFormula(0), &byteFormula(n));
  TEST_ASSERT_EQUAL_PTR(&byteFormula(0), &byteFormula(-1));
  TEST_ASSERT_EQUAL_PTR(&byteCode(0), &byteCode(n));
}

void test_lib_reference_formulas() {
  int n = byteFormulaCount();
  auto find = [n](const char* name) {
    for (int i = 0; i < n; ++i)
      if (strcmp(byteFormula(i).name, name) == 0) return i;
    TEST_FAIL_MESSAGE(name);
    return 0;
  };
  const ByteFormula& f42 = byteFormula(find("42MELODY"));
  const ByteFormula& fviz = byteFormula(find("VIZNUT1"));
  const ByteFormula& flost = byteFormula(find("LOSTSPC"));
  const ByteCode& m42 = byteCode(find("42MELODY"));
  const ByteCode& viz = byteCode(find("VIZNUT1"));
  const ByteCode& lost = byteCode(find("LOSTSPC"));
  for (uint32_t t = 0; t < 65536; ++t) {
    TEST_ASSERT_EQUAL(static_cast<int32_t>(t * (42 & t >> 10)), byteEval(m42, t, f42.a, f42.b, f42.c));
    TEST_ASSERT_EQUAL(static_cast<int32_t>(t * ((t >> 12 | t >> 8) & 63 & t >> 4)),
                      byteEval(viz, t, fviz.a, fviz.b, fviz.c));
    TEST_ASSERT_EQUAL(static_cast<int32_t>((t * (t >> 8 | t >> 9) & 46 & t >> 8) ^ (t & t >> 13 | t >> 6)),
                      byteEval(lost, t, flost.a, flost.b, flost.c));
  }
}

// The 21 original entries before A B C (2026-10-22): with its defaults every rewritten formula
// gives exactly the old output.
void test_lib_classics_defaults_bit_identical() {
  static const char* const kOld[] = {
      "t*(42&t>>10)",
      "t*((t>>12|t>>8)&63&t>>4)",
      "(t*(t>>5|t>>8))>>(t>>16)",
      "t*((t>>9|t>>13)&25&t>>6)",
      "t*(t>>11&t>>8&123&t>>3)",
      "t&t>>8",
      "t*5&t>>7|t*3&t>>10",
      "(t>>7|t|t>>6)*10+4*(t&t>>13|t>>6)",
      "(t*(t>>8|t>>9)&46&t>>8)^(t&t>>13|t>>6)",
      "t*(t^t+(t>>15|1)^(t-1280^t)>>10)",
      "(t>>6|t|t>>(t>>16))*10+((t>>11)&7)",
      "((t>>1%128)+20)*3*t>>14*t>>18",
      "(t*9&t>>4|t*5&t>>7|t*3&t/1024)-1",
      "(t>>6|t<<1)+(t>>5|t<<3|t>>3)|t>>2|t<<1",
      "t*(((t>>9)^((t>>9)-1)^1)%13)",
      "t*5&(t>>7)|t*3&(t*4>>10)",
      "t*(0xCA98>>(t>>9&14)&15)|t>>8",
      "(t&t>>12)*(t>>4|t>>8)",
      "(t<<(t>>12&1)&128?255-(t<<(t>>12&1)&255):t<<(t>>12&1)&255)-128",
      "t*(4+(t>>11&3))>>1",
      "(t*3&128)-(t*4>>(t>>13&1)&128)>>1",
  };
  constexpr int kN = static_cast<int>(sizeof(kOld) / sizeof(kOld[0]));
  TEST_ASSERT_TRUE(byteFormulaCount() > kN);
  for (int i = 0; i < kN; ++i) {
    const ByteFormula& f = byteFormula(i);
    const ByteCode old = compileOk(kOld[i], f.mode);
    const ByteCode& now = byteCode(i);
    TEST_ASSERT_TRUE_MESSAGE(strcmp(f.expr, kOld[i]) != 0, f.name);  // rewritten with a b c
    for (uint32_t t = 0; t < 131072; ++t) {
      if (byteEval(old, t, 0, 0, 0) != byteEval(now, t, f.a, f.b, f.c)) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s t=%u", f.name, static_cast<unsigned>(t));
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

void test_byte_uses() {
  TEST_ASSERT_EQUAL(0, byteUses(compileOk("t*(42&t>>10)")));
  TEST_ASSERT_EQUAL(1, byteUses(compileOk("t*(a&t>>10)")));
  TEST_ASSERT_EQUAL(2, byteUses(compileOk("t>>b")));
  TEST_ASSERT_EQUAL(4, byteUses(compileOk("t*c")));
  TEST_ASSERT_EQUAL(5, byteUses(compileOk("t*a+c")));
  TEST_ASSERT_EQUAL(7, byteUses(compileOk("(t*a|t>>b)&c")));
  // Constants whose bytes equal the A B C opcodes are skipped, whatever their width.
  ByteCode k8 = compileOk("t*1+2");
  TEST_ASSERT_EQUAL(0, byteUses(k8));
  TEST_ASSERT_EQUAL(0, byteUses(compileOk("t*0x0302")));      // K16
  TEST_ASSERT_EQUAL(0, byteUses(compileOk("t*0x03020101")));  // K32
  TEST_ASSERT_EQUAL(2, byteUses(compileOk("t*0x03020101+b")));
  ByteCode none;
  TEST_ASSERT_EQUAL(0, byteUses(none));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bytecode_pod);
  RUN_TEST(test_compile_simple);
  RUN_TEST(test_compile_ternary);
  RUN_TEST(test_compile_unary);
  RUN_TEST(test_compile_hex_and_params);
  RUN_TEST(test_compile_precedence);
  RUN_TEST(test_compile_errors);
  RUN_TEST(test_compile_stack_depth_recorded);
  RUN_TEST(test_compile_mode_and_constants);
  RUN_TEST(test_vm_div_mod_zero);
  RUN_TEST(test_vm_shift_masked);
  RUN_TEST(test_vm_deterministic);
  RUN_TEST(test_vm_malformed_is_safe);
  RUN_TEST(test_vm_sample_modes);
  RUN_TEST(test_lib_count_and_entries);
  RUN_TEST(test_lib_reference_formulas);
  RUN_TEST(test_lib_classics_defaults_bit_identical);
  RUN_TEST(test_byte_uses);
  return UNITY_END();
}
