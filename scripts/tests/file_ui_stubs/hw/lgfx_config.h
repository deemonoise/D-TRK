#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace lgfx {struct IFont {};}
namespace fonts {inline lgfx::IFont Font0,AsciiFont8x16;}
class LGFX {};
struct LGFX_Sprite {
 struct Label {std::string text;int x,y;};
 std::vector<Label> labels;
 void drawString(const char* t,int x,int y){labels.push_back({t,x,y});}
 template<class...T> void setTextColor(T...){}
 template<class...T> void setFont(T...){}
 template<class...T> void fillRect(T...){}
 template<class...T> void drawRect(T...){}
 template<class...T> void drawFastHLine(T...){}
 template<class...T> void drawFastVLine(T...){}
 template<class...T> void fillRoundRect(T...){}
 template<class...T> void drawRoundRect(T...){}
 template<class...T> void fillTriangle(T...){}
};
