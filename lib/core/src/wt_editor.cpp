#include "wt_editor.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace mt {
void WtEditor::attach(int16_t* live, int16_t* undo) {
  detach();
  if (live && undo && live != undo) { data_ = live; undo_ = undo; }
}
void WtEditor::detach() {
  data_ = undo_ = nullptr;
  anchors_ = oldAnchors_ = 0;
  mode_ = oldMode_ = WtEditMode::Wave;
  selected_ = oldSelected_ = 0;
  dirty_ = oldDirty_ = undoValid_ = stroke_ = strokeChanged_ = false;
}
const int16_t* WtEditor::frame(int f) const {
  return ready() && f >= 0 && f < kWtFrames ? data_ + f*kWtFrameLen : nullptr;
}
bool WtEditor::select(int f) {
  if (!ready() || stroke_ || f < 0 || f >= kWtFrames) return false;
  selected_ = uint8_t(f); return true;
}
bool WtEditor::anchor(int f) const { return ready() && f >= 0 && f < kWtFrames && ((anchors_ >> f) & 1); }
void WtEditor::newWave() {
  if (!ready()) return;
  for (int i=0;i<kWtFrameLen;++i) data_[i] = int16_t(std::lround(30000.0*std::sin(6.283185307179586*i/kWtFrameLen)));
  for (int f=1;f<kWtFrames;++f) memcpy(data_+f*kWtFrameLen,data_,kWtFrameLen*sizeof(int16_t));
  anchors_ = (uint64_t(1)<<63)|1; selected_=0; mode_=WtEditMode::Wave;
  dirty_=undoValid_=stroke_=false;
}
void WtEditor::loaded() {
  if (!ready()) return;
  anchors_=UINT64_MAX; selected_=0; mode_=WtEditMode::Morph; dirty_=undoValid_=stroke_=false;
}
void WtEditor::snapshot() {
  memcpy(undo_,data_,kWtSrcSamples*sizeof(int16_t));
  oldAnchors_=anchors_; oldSelected_=selected_; oldMode_=mode_; oldDirty_=dirty_; undoValid_=true;
}
void WtEditor::rebuild() {
  if (mode_==WtEditMode::Wave) {
    const int16_t* src=data_+selected_*kWtFrameLen;
    for(int f=0;f<kWtFrames;++f) if(f!=selected_) memcpy(data_+f*kWtFrameLen,src,kWtFrameLen*sizeof(int16_t));
    return;
  }
  int left=0;
  for(int right=1;right<kWtFrames;++right) if(anchor(right)) {
    for(int f=left+1;f<right;++f) for(int i=0;i<kWtFrameLen;++i) {
      int32_t a=data_[left*kWtFrameLen+i], b=data_[right*kWtFrameLen+i];
      data_[f*kWtFrameLen+i]=int16_t(a+(b-a)*(f-left)/(right-left));
    }
    left=right;
  }
}
void WtEditor::changed() { dirty_=true; rebuild(); }
bool WtEditor::setMode(WtEditMode mode) {
  if (!ready() || stroke_ || mode==mode_ || (mode!=WtEditMode::Wave && mode!=WtEditMode::Morph)) return false;
  snapshot(); mode_=mode; anchors_=(uint64_t(1)<<63)|1; changed(); return true;
}
bool WtEditor::addAnchor() {
  if (!ready() || stroke_ || mode_!=WtEditMode::Morph || anchor(selected_)) return false;
  snapshot(); anchors_|=uint64_t(1)<<selected_; dirty_=true; return true;
}
bool WtEditor::copyAnchor(int from,int to) {
  if (!ready() || stroke_ || mode_!=WtEditMode::Morph || from<0 || from>=kWtFrames || to<0 || to>=kWtFrames || anchor(to)) return false;
  snapshot(); memcpy(data_+to*kWtFrameLen,data_+from*kWtFrameLen,kWtFrameLen*sizeof(int16_t));
  anchors_|=uint64_t(1)<<to; selected_=uint8_t(to); changed(); return true;
}
bool WtEditor::deleteAnchor() {
  if (!ready() || stroke_ || mode_!=WtEditMode::Morph || selected_==0 || selected_==63 || !anchor(selected_)) return false;
  snapshot(); anchors_ &= ~(uint64_t(1)<<selected_); changed(); return true;
}
bool WtEditor::beginStroke() {
  if (!ready() || stroke_) return false;
  stroke_=true; strokeChanged_=false; return true;
}
bool WtEditor::drawLine(int x0,int16_t y0,int x1,int16_t y1) {
  if (!ready() || !stroke_) return false;
  x0=std::max(0,std::min(kWtFrameLen-1,x0)); x1=std::max(0,std::min(kWtFrameLen-1,x1));
  if(x0>x1) { std::swap(x0,x1); std::swap(y0,y1); }
  int16_t* out=data_+selected_*kWtFrameLen;
  const auto valueAt = [=](int i) -> int16_t {
    return x0 == x1 ? y1 : int16_t(int32_t(y0)+(int32_t(y1)-y0)*(i-x0)/(x1-x0));
  };
  bool differs = false;
  for (int i=x0;i<=x1;++i) if (out[i] != valueAt(i)) { differs=true; break; }
  if (!differs) return false;
  if (!strokeChanged_) { snapshot(); strokeChanged_=true; }
  for (int i=x0;i<=x1;++i) out[i]=valueAt(i);
  if(mode_==WtEditMode::Morph) anchors_|=uint64_t(1)<<selected_;
  changed(); return true;
}
void WtEditor::endStroke() { stroke_=strokeChanged_=false; }
bool WtEditor::shape(WtShape shape) {
  if (!ready() || stroke_ || shape>=WtShape::Count) return false;
  snapshot();
  float points[kWtFrameLen]; wtShape(shape,points);
  for(int i=0;i<kWtFrameLen;++i) data_[selected_*kWtFrameLen+i]=int16_t(std::lround(points[i]*kWtMakePeak));
  if(mode_==WtEditMode::Morph) anchors_|=uint64_t(1)<<selected_;
  changed(); return true;
}
bool WtEditor::smooth() {
  if (!ready() || stroke_) return false;
  snapshot();
  const int16_t* src=undo_+selected_*kWtFrameLen;
  for(int i=0;i<kWtFrameLen;++i) data_[selected_*kWtFrameLen+i]=int16_t((int32_t(src[(i+255)%256])+2*int32_t(src[i])+src[(i+1)%256])/4);
  if(mode_==WtEditMode::Morph) anchors_|=uint64_t(1)<<selected_;
  changed(); return true;
}
bool WtEditor::undo() {
  if (!ready() || stroke_ || !undoValid_) return false;
  memcpy(data_,undo_,kWtSrcSamples*sizeof(int16_t));
  anchors_=oldAnchors_; selected_=oldSelected_; mode_=oldMode_; dirty_=oldDirty_; undoValid_=false; return true;
}
} // namespace mt
