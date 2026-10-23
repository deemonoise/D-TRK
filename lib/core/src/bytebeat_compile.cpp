#include "bytebeat_compile.h"

namespace mt {
namespace {

enum class Tok : uint8_t { End, Num, Var, LParen, RParen, Quest, Colon, Op, Bad };

// Binary operators, C precedence: higher binds tighter.
struct BinOp {
  const char* s;
  uint8_t prec;
  ByteOp op;
};
constexpr BinOp kBin[] = {
    {"<<", 8, ByteOp::Shl}, {">>", 8, ByteOp::Shr}, {"<=", 7, ByteOp::Le}, {">=", 7, ByteOp::Ge},
    {"==", 6, ByteOp::Eq},  {"!=", 6, ByteOp::Ne},  {"&&", 2, ByteOp::LAnd}, {"||", 1, ByteOp::LOr},
    {"*", 10, ByteOp::Mul}, {"/", 10, ByteOp::Div}, {"%", 10, ByteOp::Mod}, {"+", 9, ByteOp::Add},
    {"-", 9, ByteOp::Sub},  {"<", 7, ByteOp::Lt},   {">", 7, ByteOp::Gt},   {"&", 5, ByteOp::And},
    {"^", 4, ByteOp::Xor},  {"|", 3, ByteOp::Or},   {"!", 0, ByteOp::LNot}, {"~", 0, ByteOp::Not},
};  // two-char entries first; "!" and "~" are unary only (prec 0)

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
int hexVal(char c) {
  if (isDigit(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

class Compiler {
 public:
  Compiler(const char* src, ByteCode& out) : s_(src), out_(out) {}

  ByteCompileResult run() {
    next();
    if (tok_ == Tok::End && !err_) fail(ByteErr::Empty, tokPos_);
    parseExpr();
    if (!err_ && tok_ != Tok::End) fail(tok_ == Tok::RParen ? ByteErr::Unbalanced : ByteErr::Syntax, tokPos_);
    ByteCompileResult r;
    r.err = err_ ? errCode_ : ByteErr::Ok;
    r.pos = static_cast<uint16_t>(errPos_);
    return r;
  }
  int maxDepth() const { return maxSp_; }

 private:
  void fail(ByteErr e, int pos) {
    if (err_) return;
    err_ = true;
    errCode_ = e;
    errPos_ = pos;
  }

  // Reads the next token into tok_ / tokPos_ / num_ / op_.
  void next() {
    if (err_) {
      tok_ = Tok::End;
      return;
    }
    while (s_[p_] == ' ' || s_[p_] == '\t') ++p_;
    tokPos_ = p_;
    const char c = s_[p_];
    if (p_ > kByteSrcMax) {
      fail(ByteErr::TooLong, p_);
      tok_ = Tok::End;
      return;
    }
    if (c == 0) {
      tok_ = Tok::End;
      return;
    }
    if (isDigit(c)) return number();
    if (isAlpha(c)) {
      int e = p_;
      while (isAlpha(s_[e]) || isDigit(s_[e])) ++e;
      if (e - p_ == 1 && (c == 't' || c == 'a' || c == 'b' || c == 'c')) {
        tok_ = Tok::Var;
        op_ = c == 't' ? ByteOp::T : c == 'a' ? ByteOp::A : c == 'b' ? ByteOp::B : ByteOp::C;
        p_ = e;
        return;
      }
      return bad();
    }
    switch (c) {
      case '(': tok_ = Tok::LParen; ++p_; return;
      case ')': tok_ = Tok::RParen; ++p_; return;
      case '?': tok_ = Tok::Quest; ++p_; return;
      case ':': tok_ = Tok::Colon; ++p_; return;
      default: break;
    }
    for (const BinOp& b : kBin) {
      const bool two = b.s[1] != 0;
      if (b.s[0] == c && (!two || s_[p_ + 1] == b.s[1])) {
        tok_ = Tok::Op;
        op_ = b.op;
        prec_ = b.prec;
        p_ += two ? 2 : 1;
        return;
      }
    }
    bad();
  }

  void bad() {
    fail(ByteErr::BadToken, p_);
    tok_ = Tok::Bad;
  }

  void number() {
    uint64_t v = 0;
    int e = p_;
    if (s_[e] == '0' && (s_[e + 1] == 'x' || s_[e + 1] == 'X')) {
      e += 2;
      if (hexVal(s_[e]) < 0) return bad();
      while (hexVal(s_[e]) >= 0) {
        v = v * 16 + static_cast<uint64_t>(hexVal(s_[e++]));
        if (v > 0xFFFFFFFFu) return bad();
      }
    } else {
      while (isDigit(s_[e])) {
        v = v * 10 + static_cast<uint64_t>(s_[e++] - '0');
        if (v > 0xFFFFFFFFu) return bad();
      }
    }
    if (isAlpha(s_[e])) return bad();  // 12abc
    num_ = static_cast<uint32_t>(v);
    tok_ = Tok::Num;
    p_ = e;
  }

  void emit(ByteOp op, int pos) {
    if (err_) return;
    const int pops = byteOpPops(static_cast<uint8_t>(op));
    sp_ += 1 - pops;
    if (sp_ > maxSp_) maxSp_ = sp_;
    if (maxSp_ > kByteStackMax) return fail(ByteErr::TooDeep, pos);
    if (out_.len + 1 > kByteOpsMax) return fail(ByteErr::TooLong, pos);
    out_.ops[out_.len++] = static_cast<uint8_t>(op);
  }

  void emitNum(uint32_t v, int pos) {
    const ByteOp op = v < 0x100 ? ByteOp::K8 : v < 0x10000 ? ByteOp::K16 : ByteOp::K32;
    const int n = byteOpImm(static_cast<uint8_t>(op));
    if (!err_ && out_.len + 1 + n > kByteOpsMax) return fail(ByteErr::TooLong, pos);
    emit(op, pos);
    if (err_) return;
    for (int i = 0; i < n; ++i) out_.ops[out_.len++] = static_cast<uint8_t>(v >> (8 * i));
  }

  bool enter() {
    if (++nest_ > kByteNestMax) fail(ByteErr::TooDeep, tokPos_);
    return !err_;
  }

  // expr := binary ( '?' expr ':' expr )?
  void parseExpr() {
    if (!enter()) return;
    parseBinary(1);
    if (!err_ && tok_ == Tok::Quest) {
      next();
      parseExpr();
      if (err_) return;
      if (tok_ != Tok::Colon) return fail(ByteErr::Syntax, tokPos_);
      next();
      parseExpr();
      emit(ByteOp::Sel, tokPos_);
    }
    --nest_;
  }

  // Precedence climbing over left-associative binary operators.
  void parseBinary(int minPrec) {
    parseUnary();
    while (!err_ && tok_ == Tok::Op && prec_ >= minPrec) {
      const ByteOp op = op_;
      const int prec = prec_, pos = tokPos_;
      next();
      parseBinary(prec + 1);
      emit(op, pos);
    }
  }

  void parseUnary() {
    if (err_) return;
    if (tok_ == Tok::Op && (op_ == ByteOp::Sub || op_ == ByteOp::Not || op_ == ByteOp::LNot)) {
      const ByteOp op = op_ == ByteOp::Sub ? ByteOp::Neg : op_;
      const int pos = tokPos_;
      if (!enter()) return;
      next();
      parseUnary();
      emit(op, pos);
      --nest_;
      return;
    }
    parsePrimary();
  }

  void parsePrimary() {
    if (err_) return;
    const int pos = tokPos_;
    switch (tok_) {
      case Tok::Num:
        emitNum(num_, pos);
        next();
        return;
      case Tok::Var:
        emit(op_, pos);
        next();
        return;
      case Tok::LParen:
        next();
        parseExpr();
        if (err_) return;
        if (tok_ != Tok::RParen) return fail(tok_ == Tok::End ? ByteErr::Unbalanced : ByteErr::Syntax, tokPos_);
        next();
        return;
      default:
        fail(ByteErr::Syntax, pos);  // operand missing
        return;
    }
  }

  const char* s_;
  ByteCode& out_;
  int p_ = 0, tokPos_ = 0;
  Tok tok_ = Tok::End;
  uint32_t num_ = 0;
  ByteOp op_ = ByteOp::T;
  int prec_ = 0;
  int sp_ = 0, maxSp_ = 0, nest_ = 0;
  bool err_ = false;
  ByteErr errCode_ = ByteErr::Ok;
  int errPos_ = 0;
};

}  // namespace

ByteCompileResult byteCompile(const char* src, uint8_t mode, ByteCode& out) {
  out = ByteCode();
  ByteCompileResult r;
  if (mode > kByteSigned) {
    r.err = ByteErr::BadMode;
    return r;
  }
  if (!src) src = "";
  Compiler c(src, out);
  r = c.run();
  if (r.err != ByteErr::Ok) {
    out = ByteCode();
    return r;
  }
  out.mode = mode;
  out.depth = static_cast<uint8_t>(c.maxDepth());
  return r;
}

}  // namespace mt
