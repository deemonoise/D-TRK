#pragma once
#include <stdint.h>

namespace mt {

// Bytebeat bytecode and its interpreter. Header-only and self-contained: on a split build the
// compiler runs on one chip and the ByteCode travels to the one that renders.

constexpr uint8_t kByteUnsigned = 0;  // sample = (x & 255) - 128
constexpr uint8_t kByteSigned = 1;    // sample = (int8_t)x
constexpr int kByteOpsMax = 61;       // ops bytes, ByteCode is 64 bytes
constexpr int kByteStackMax = 16;     // evaluation stack depth, checked by the compiler

// RPN opcodes, one byte each; K8 / K16 / K32 are followed by a little-endian constant.
enum class ByteOp : uint8_t {
  T, A, B, C, K8, K16, K32,
  Neg, Not, LNot,                        // unary - ~ !
  Mul, Div, Mod, Add, Sub, Shl, Shr,     // binary, uint32; / % by 0 = 0, shift counts & 31
  And, Xor, Or, Lt, Gt, Le, Ge, Eq, Ne,  // compares are unsigned, give 0 / 1
  LAnd, LOr,                             // both sides always evaluated
  Sel,                                   // c x y -> c ? x : y
  Count
};

// Compiled formula: plain bytes, no pointers, safe to memcpy / send over a link.
struct ByteCode {
  uint8_t len = 0;    // bytes used in ops, 0 = nothing compiled
  uint8_t mode = kByteUnsigned;
  uint8_t depth = 0;  // max stack depth while evaluating
  uint8_t ops[kByteOpsMax] = {};
};
static_assert(sizeof(ByteCode) == 64, "ByteCode layout");

// Stack effect of an opcode (pushes - pops) and pops; constants / variables push 1.
inline int byteOpPops(uint8_t op) {
  if (op <= static_cast<uint8_t>(ByteOp::K32)) return 0;
  if (op <= static_cast<uint8_t>(ByteOp::LNot)) return 1;
  return op == static_cast<uint8_t>(ByteOp::Sel) ? 3 : 2;
}
inline int byteOpImm(uint8_t op) {
  return op == static_cast<uint8_t>(ByteOp::K8) ? 1 : op == static_cast<uint8_t>(ByteOp::K16) ? 2
         : op == static_cast<uint8_t>(ByteOp::K32) ? 4 : 0;
}

// Which of a b c the code reads: bit 0 a, bit 1 b, bit 2 c (constants' bytes are skipped). Unused
// macros are greyed out in the UI.
inline uint8_t byteUses(const ByteCode& bc) {
  uint8_t m = 0;
  const int n = bc.len <= kByteOpsMax ? bc.len : kByteOpsMax;
  for (int i = 0; i < n; ++i) {
    const uint8_t op = bc.ops[i];
    if (op >= static_cast<uint8_t>(ByteOp::A) && op <= static_cast<uint8_t>(ByteOp::C))
      m |= 1u << (op - static_cast<uint8_t>(ByteOp::A));
    i += byteOpImm(op);
  }
  return m;
}

// True when bc is well formed: known opcodes, whole constants, the stack never underflows, stays
// within kByteStackMax, ends with one value, and depth matches. For bytecode from outside.
inline bool byteVerify(const ByteCode& bc) {
  if (bc.len == 0 || bc.len > kByteOpsMax || bc.mode > kByteSigned) return false;
  int sp = 0, mx = 0;
  for (int i = 0; i < bc.len; ++i) {
    uint8_t op = bc.ops[i];
    if (op >= static_cast<uint8_t>(ByteOp::Count)) return false;
    int pops = byteOpPops(op);
    if (sp < pops) return false;
    sp += 1 - pops;
    mx = sp > mx ? sp : mx;
    if (mx > kByteStackMax) return false;
    i += byteOpImm(op);
    if (i >= bc.len) return false;
  }
  return sp == 1 && mx == bc.depth;
}

// Evaluates bc at tick t with parameters a b c; returns the raw 32-bit result. Straight-line: one
// pass over at most kByteOpsMax bytes. Malformed code cannot run off the stack or the ops (indices
// are masked, truncated constants stop it); it just gives a meaningless value.
inline int32_t byteEval(const ByteCode& bc, uint32_t t, int a, int b, int c) {
  constexpr unsigned kMask = kByteStackMax - 1;
  static_assert((kByteStackMax & kMask) == 0, "stack size is a power of two");
  uint32_t s[kByteStackMax] = {};
  unsigned sp = 0;  // items on the stack; top is s[(sp - 1) & kMask]
  const unsigned n = bc.len <= kByteOpsMax ? bc.len : kByteOpsMax;
  for (unsigned i = 0; i < n; ++i) {
    const uint8_t op = bc.ops[i];
    if (op <= static_cast<uint8_t>(ByteOp::K32)) {
      uint32_t v;
      switch (static_cast<ByteOp>(op)) {
        case ByteOp::T: v = t; break;
        case ByteOp::A: v = static_cast<uint32_t>(a); break;
        case ByteOp::B: v = static_cast<uint32_t>(b); break;
        case ByteOp::C: v = static_cast<uint32_t>(c); break;
        case ByteOp::K8:
          if (i + 1 >= n) return 0;
          v = bc.ops[i + 1];
          i += 1;
          break;
        case ByteOp::K16:
          if (i + 2 >= n) return 0;
          v = bc.ops[i + 1] | static_cast<uint32_t>(bc.ops[i + 2]) << 8;
          i += 2;
          break;
        default:  // K32
          if (i + 4 >= n) return 0;
          v = bc.ops[i + 1] | static_cast<uint32_t>(bc.ops[i + 2]) << 8 | static_cast<uint32_t>(bc.ops[i + 3]) << 16 |
              static_cast<uint32_t>(bc.ops[i + 4]) << 24;
          i += 4;
          break;
      }
      s[sp++ & kMask] = v;
      continue;
    }
    uint32_t& x = s[(sp - 1) & kMask];
    switch (static_cast<ByteOp>(op)) {
      case ByteOp::Neg: x = 0u - x; continue;
      case ByteOp::Not: x = ~x; continue;
      case ByteOp::LNot: x = x == 0; continue;
      default: break;
    }
    if (op == static_cast<uint8_t>(ByteOp::Sel)) {
      const uint32_t y = s[(sp - 1) & kMask], xv = s[(sp - 2) & kMask];
      sp -= 2;
      uint32_t& cnd = s[(sp - 1) & kMask];
      cnd = cnd ? xv : y;
      continue;
    }
    const uint32_t y = x;
    --sp;
    uint32_t& l = s[(sp - 1) & kMask];
    switch (static_cast<ByteOp>(op)) {
      case ByteOp::Mul: l *= y; break;
      case ByteOp::Div: l = y ? l / y : 0; break;
      case ByteOp::Mod: l = y ? l % y : 0; break;
      case ByteOp::Add: l += y; break;
      case ByteOp::Sub: l -= y; break;
      case ByteOp::Shl: l <<= (y & 31); break;
      case ByteOp::Shr: l >>= (y & 31); break;
      case ByteOp::And: l &= y; break;
      case ByteOp::Xor: l ^= y; break;
      case ByteOp::Or: l |= y; break;
      case ByteOp::Lt: l = l < y; break;
      case ByteOp::Gt: l = l > y; break;
      case ByteOp::Le: l = l <= y; break;
      case ByteOp::Ge: l = l >= y; break;
      case ByteOp::Eq: l = l == y; break;
      case ByteOp::Ne: l = l != y; break;
      case ByteOp::LAnd: l = l && y; break;
      case ByteOp::LOr: l = l || y; break;
      default: break;  // unknown opcode: drops the top
    }
  }
  return sp ? static_cast<int32_t>(s[(sp - 1) & kMask]) : 0;
}

// Raw result -> sample in [-1, 1) for the formula's mode.
inline float byteSample(int32_t x, uint8_t mode) {
  const int v = mode == kByteSigned ? static_cast<int8_t>(x) : (x & 255) - 128;
  return static_cast<float>(v) * (1.f / 128.f);
}

}  // namespace mt
