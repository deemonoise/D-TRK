#pragma once
namespace ui {
class App;
class WifiDialog {
 public:
 explicit WifiDialog(App&){}
 bool open(){opened=true;return true;}
 void close(){opened=false;}
 bool isOpen()const{return opened;}
 void poll(){}
 bool wantsRedraw(){return false;}
 void onInput(const hw::InputEvent&){}
 void onTouch(const TouchEvent&){}
 void draw(LGFX_Sprite& s,int y){s.drawString("WI-FI",16,y);}
 private:bool opened=false;
};
}
