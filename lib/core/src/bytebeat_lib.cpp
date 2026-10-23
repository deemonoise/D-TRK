#include "bytebeat_lib.h"
#include "bytebeat_compile.h"

namespace mt {
namespace {

// Classics from the 2011 "algorithmic symphonies from one line of code" experiments (viznut's
// countercomplex blog posts and videos, the pouet.net thread), written down from the published
// one-liners; t runs at 8 kHz at C-4 like the originals. Then D-TRK entries that use a b c.
constexpr ByteFormula kFormulas[] = {
    // "42 melody": anonymous, from the countercomplex comments, 2011. t*(42&t>>10)
    {"42MELODY", "t*(a&t>>b)", kByteUnsigned, 42, 10, 0},
    // viznut, first video, 2011. t*((t>>12|t>>8)&63&t>>4)
    {"VIZNUT1", "t*((t>>a|t>>8)&b&t>>c)", kByteUnsigned, 12, 63, 4},
    // tejeez, first video, 2011. (t*(t>>5|t>>8))>>(t>>16)
    {"TEJEEZ1", "(t*(t>>a|t>>b))>>(t>>c)", kByteUnsigned, 5, 8, 16},
    // visy, first video, 2011. t*((t>>9|t>>13)&25&t>>6)
    {"VISY", "t*((t>>a|t>>13)&b&t>>c)", kByteUnsigned, 9, 25, 6},
    // tejeez, first video, 2011. t*(t>>11&t>>8&123&t>>3)
    {"TEJEEZ2", "t*(t>>a&t>>b&c&t>>3)", kByteUnsigned, 11, 8, 123},
    // "Sierpinski harmony": the classic t&t>>8, discussed by viznut, 2011. t&t>>8
    {"SIERPNSK", "t*b&t>>a", kByteUnsigned, 8, 1, 0},
    // viznut, second video, 2011. t*5&t>>7|t*3&t>>10
    {"VIZNUT2", "t*a&t>>7|t*b&t>>c", kByteUnsigned, 5, 3, 10},
    // viznut, skurk and raer, 2011. (t>>7|t|t>>6)*10+4*(t&t>>13|t>>6)
    {"SKURK", "(t>>a|t|t>>6)*b+4*(t&t>>c|t>>6)", kByteUnsigned, 7, 10, 13},
    // xpansive, "Lost in Space", 2011. (t*(t>>8|t>>9)&46&t>>8)^(t&t>>13|t>>6)
    {"LOSTSPC", "(t*(t>>8|t>>9)&a&t>>8)^(t&t>>b|t>>c)", kByteUnsigned, 46, 13, 6},
    // blueberry, 2011. t*(t^t+(t>>15|1)^(t-1280^t)>>10); 1280 = c<<8
    {"BLUEBRY", "t*(t^t+(t>>a|1)^(t-(c<<8)^t)>>b)", kByteUnsigned, 15, 10, 5},
    // viznut, 2011. (t>>6|t|t>>(t>>16))*10+((t>>11)&7)
    {"VIZNUT3", "(t>>a|t|t>>(t>>16))*b+((t>>c)&7)", kByteUnsigned, 6, 10, 11},
    // harism, 2011 (shift counts wrap & 31 here). ((t>>1%128)+20)*3*t>>14*t>>18
    {"HARISM", "((t>>1%128)+a)*b*t>>c*t>>18", kByteUnsigned, 20, 3, 14},
    // stephth, 2011. (t*9&t>>4|t*5&t>>7|t*3&t/1024)-1
    {"STEPHTH", "(t*a&t>>4|t*b&t>>7|t*c&t/1024)-1", kByteUnsigned, 9, 5, 3},
    // bear @ celephais, 2011. (t>>6|t<<1)+(t>>5|t<<3|t>>3)|t>>2|t<<1
    {"BEAR", "(t>>a|t<<1)+(t>>b|t<<3|t>>3)|t>>c|t<<1", kByteUnsigned, 6, 5, 2},
    // rrola, 2011. t*(((t>>9)^((t>>9)-1)^1)%13)
    {"RROLA", "t*(((t>>a)^((t>>a)-1)^c)%b)", kByteUnsigned, 9, 13, 1},
    // miiro, 2011. t*5&(t>>7)|t*3&(t*4>>10)
    {"MIIRO", "t*a&(t>>7)|t*b&(t*4>>c)", kByteUnsigned, 5, 3, 10},
    // viznut, melody from a nibble table in a constant, 2011. t*(0xCA98>>(t>>9&14)&15)|t>>8
    {"NIBBLES", "t*(0xCA98>>(t>>a&14)&c)|t>>b", kByteUnsigned, 9, 8, 15},
    // anonymous, countercomplex comments, 2011. (t&t>>12)*(t>>4|t>>8)
    {"CROWD", "(t&t>>a)*(t>>b|t>>c)", kByteUnsigned, 12, 4, 8},
    // D-TRK: signed triangle, octave steps. (t<<(t>>12&1)&128?255-(t<<(t>>12&1)&255):t<<(t>>12&1)&255)-128
    {"SGNTRI", "(t<<(t>>a&1)&128?255-(t<<(t>>a&1)&255):t<<(t>>a&1)&255)-128", kByteSigned, 12, 0, 0},
    // D-TRK: signed sawtooth arpeggio. t*(4+(t>>11&3))>>1
    {"SGNARP", "t*(a+(t>>b&c))>>1", kByteSigned, 4, 11, 3},
    // D-TRK: signed 1-bit duet. (t*3&128)-(t*4>>(t>>13&1)&128)>>1
    {"SGNDUET", "(t*a&128)-(t*b>>(t>>c&1)&128)>>1", kByteSigned, 3, 4, 13},
    // D-TRK: "42 melody" with a as the mask (a = 42 is the original)
    {"MELODY A", "t*(a&t>>10)", kByteUnsigned, 42, 0, 0},
    // D-TRK: Sierpinski, a = rhythm shift, b = pitch
    {"SIERP AB", "t*(b>>4|1)&t>>(a>>4|4)", kByteUnsigned, 112, 16, 0},
    // D-TRK: two square voices, a b = pitches, c = rhythm mask
    {"SQUARES", "(t*(a>>3|1)&128|t*(b>>3|1)&128)&(t>>(c>>4|8)&3?255:0)", kByteUnsigned, 40, 60, 20},
    // D-TRK: fractal bells, a b c bend the masks
    {"BELLS", "t*((t>>9|t>>(b>>4|8))&(a|7)&t>>(c>>5|3))", kByteUnsigned, 50, 70, 90},
    // D-TRK: signed pulse, a = pitch, b = level
    {"SGNPULSE", "t*(a>>2|1)&64?b:-b", kByteSigned, 16, 100, 0},
    // More 2011 one-liners (the countercomplex posts and comments, the pouet.net thread, viznut's
    // video collections), written down from the published expressions; a b c replace constants and
    // the defaults give the original. Author named where those sources name one.
    // tejeez, first video, 2011. t*(t>>8*(t>>15|t>>8)&(20|(t>>19)*5>>t|t>>3))
    {"TEJEEZ3", "t*(t>>a*(t>>15|t>>a)&(b|(t>>19)*5>>t|t>>c))", kByteUnsigned, 8, 20, 3},
    // 2011 collections. ((t&4096)?((t*(t^t%255)|(t>>4))>>1):(t>>3)|((t&8192)?t<<2:t))
    {"BITGATE", "(t>>a&1)?((t*(t^t%255)|(t>>c))>>1):(t>>3)|((t>>b&1)?t<<2:t)", kByteUnsigned, 12, 13, 4},
    // red-, 2011. (t|(t>>9|t>>7))*t&(t>>11|t>>9)
    {"RED", "(t|(t>>a|t>>c))*t&(t>>b|t>>a)", kByteUnsigned, 9, 11, 7},
    // 2011 collections. t*(t>>((t>>9)|(t>>8))&(63&(t>>4)))
    {"SHIFTMEL", "t*(t>>((t>>a)|(t>>8))&(b&(t>>c)))", kByteUnsigned, 9, 63, 4},
    // 2011 collections. t>>6&1?t>>5:-t>>4
    {"SAWFLIP", "t>>a&1?t>>b:-t>>c", kByteUnsigned, 6, 5, 4},
    // 2011 collections. (t*((t>>9|t>>13)&15))&129
    {"MASK129", "(t*((t>>a|t>>c)&b))&129", kByteUnsigned, 9, 15, 13},
    // 2011 collections. t>>16|((t>>4)%16)|((t>>4)%192)|(t*t%64)|(t*t%96)|(t>>16)*(t|t>>5)
    {"MODMIX", "t>>16|((t>>4)%a)|((t>>4)%192)|(t*t%b)|(t*t%c)|(t>>16)*(t|t>>5)", kByteUnsigned, 16, 64, 96},
    // 2011 collections. (t>>4)|(t%10)|(((t%101)|(t>>14))&((t>>7)|(t*t%17)))
    {"MODSEQ", "(t>>4)|(t%a)|(((t%b)|(t>>14))&((t>>7)|(t*t%c)))", kByteUnsigned, 10, 101, 17},
    // 2011 collections. (t>>5)|(t<<4)|((t&1023)^1981)|((t-67)>>4)
    {"ODDSHIFT", "(t>>a)|(t<<b)|((t&1023)^1981)|((t-c)>>b)", kByteUnsigned, 5, 4, 67},
    // D-TRK rhythm: a step is 1024 t (t>>10 = the step, t>>14 = the bar of 16), so in SYNC they
    // lock to the 1/16 grid. Patterns are 7-bit masks over steps 0..6 of each half bar.
    // D-TRK: kick, a / c = pattern of the first / second half bar, b = pitch (lower = higher)
    {"KICK", "((t>>13&1?c:a)>>(t>>10&7)&1)&&((t&1023)*(2048-(t&1023))>>b&128)?255-(t&1023)/4:0",
     kByteUnsigned, 17, 8, 81},
    // D-TRK: noise hats, a = accent pattern, b = hits per step
    {"HATS", "(t*t*0x9E3779B1>>24)*(255-(t*b&1023)/4)*(a>>(t>>10&7)&1?4:1)>>10", kByteUnsigned, 85, 1, 0},
    // D-TRK: kick on step 0, noise snare on step 4 of a = 7 (a = 15: of 16), b = kick pitch,
    // c = snare length
    {"BACKBEAT", "(t>>10&a)==0?(t&1023)*(2048-(t&1023))>>b&128:(t>>10&a)==4&(t&1023)<c*32?t*t*t>>11:0",
     kByteUnsigned, 7, 8, 16},
    // D-TRK: major arpeggio, a = steps per note (8: one, 9: two), b = gate, c = octaves down
    {"ARP", "(t&1023)<b*8?t*(0x8654>>(t>>a&12)&15)>>c:0", kByteUnsigned, 8, 96, 0},
    // D-TRK: minor arpeggio over 8 steps, a = speed, b = octaves down
    {"ARPMIN", "t*(0xA6ACFCA5>>(t>>a&28)&15)>>b", kByteUnsigned, 8, 1, 0},
    // D-TRK: square bass over 8 steps, a = speed, b = pattern (rests), c = pitch
    {"BASSLN", "b>>(t>>10&7)&1?t*c*(0x34224362>>(t>>a&28)&15)>>2&128:64", kByteUnsigned, 8, 119, 2},
    // D-TRK: octave-jumping 16th bass, a / c = root of the even / odd bars, b = octaves down
    {"OCTBASS", "t*(t>>14&1?c:a)<<(t>>10&1)>>b&128", kByteUnsigned, 8, 2, 12},
    // D-TRK: chord stabs on a 16-step pattern, a b c = the three notes (8 10 12: a major triad)
    {"CHORD", "((t*a&255)+(t*b&255)+(t*c&255))/3*(0x5AD5>>(t>>10&15)&1)", kByteUnsigned, 8, 10, 12},
    // D-TRK: 16-step melody (two 8-step tables), a = octaves down, b = gate
    {"MELSEQ", "(t&1023)<b*8?t*((t>>13&1?0x5986A865:0x86A5C865)>>(t>>8&28)&15)>>a:0", kByteUnsigned, 1, 100, 0},
    // D-TRK: pulse arpeggio, a = steps per note, b = pulse width, c = width sweep speed
    {"PWMARP", "(t*(0x8654>>(t>>a&12)&15)&255)<b+(t>>c&127)?255:0", kByteUnsigned, 8, 64, 7},
};
constexpr int kCount = static_cast<int>(sizeof(kFormulas) / sizeof(kFormulas[0]));

struct Table {
  ByteCode code[kCount];
  Table() {
    for (int i = 0; i < kCount; ++i) byteCompile(kFormulas[i].expr, kFormulas[i].mode, code[i]);
  }
};

const Table& table() {
  static const Table t;
  return t;
}

int clampIndex(int i) { return i >= 0 && i < kCount ? i : 0; }

}  // namespace

int byteFormulaCount() {
  table();
  return kCount;
}

const ByteFormula& byteFormula(int i) { return kFormulas[clampIndex(i)]; }

const ByteCode& byteCode(int i) { return table().code[clampIndex(i)]; }

}  // namespace mt
