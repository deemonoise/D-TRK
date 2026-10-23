#pragma once
#include <stdint.h>
#include "bytebeat_vm.h"

namespace mt {

// Bytebeat formula compiler: infix C-like expression -> RPN ByteCode.
// Grammar: decimal / hex (0x) literals up to 32 bits, variables t a b c, unary - ~ !, binary
// * / % + - << >> < > <= >= == != & ^ | && ||, ternary ?:, parentheses; C precedence and
// associativity. Arithmetic is uint32 (see bytebeat_vm.h). Spaces / tabs ignored.

constexpr int kByteSrcMax = 255;  // source length limit
constexpr int kByteNestMax = 32;  // parentheses / unary / ternary nesting limit

enum class ByteErr : uint8_t {
  Ok,
  Empty,       // nothing but blanks
  BadToken,    // unknown character or name, '=' alone, malformed / too big number
  Syntax,      // operand or operator missing / unexpected, ':' missing
  Unbalanced,  // '(' without ')' or the other way round
  TooLong,     // bytecode over kByteOpsMax bytes or source over kByteSrcMax
  TooDeep,     // stack over kByteStackMax or nesting over kByteNestMax
  BadMode,     // mode is neither kByteUnsigned nor kByteSigned
};

struct ByteCompileResult {
  ByteErr err = ByteErr::Ok;
  uint16_t pos = 0;  // source offset of the error
};

// Compiles src with the output mode. On error out is cleared (len 0). No heap; recursion is
// bounded by kByteNestMax.
ByteCompileResult byteCompile(const char* src, uint8_t mode, ByteCode& out);

}  // namespace mt
