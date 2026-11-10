#pragma once
namespace ui {
template<class Sprite> struct FrameBufferState {
  Sprite* retained = nullptr;  // object kept for a later create retry; it owns no pixels while loaned
  bool loaned = false;
};

template<class Sprite>
bool restoreFrameBuffer(FrameBufferState<Sprite>& state, Sprite*& published,
                        bool (*create)(Sprite&,void*), void* context) {
  if (state.loaned) return false;
  if (published) return true;
  if (!state.retained) return false;
  if (!create(*state.retained,context)) {
    state.retained->deleteSprite();
    return false;
  }
  published = state.retained;
  state.retained = nullptr;
  return true;
}

// Synchronous UI-thread lease. A nested lease never restores the outer one's pixels.
// The sprite is published only after create succeeds; failed recreation can be retried later.
template<class Sprite> class FrameBufferLease {
 public:
  FrameBufferLease(FrameBufferState<Sprite>& state, Sprite*& published,
                   void (*wait)(void*), bool (*create)(Sprite&,void*), void* context)
      : state_(state), published_(published), create_(create), context_(context) {
    if (state_.loaned) return;
    owns_ = true;
    state_.loaned = true;  // suppress progress redraw/reallocation even during the DMA wait
    if (!published_) return;
    wait(context_);
    state_.retained = published_;
    published_ = nullptr;
    state_.retained->deleteSprite();
  }
  ~FrameBufferLease() {
    if (!owns_) return;
    state_.loaned = false;
    restoreFrameBuffer(state_,published_,create_,context_);
  }
  FrameBufferLease(const FrameBufferLease&) = delete;
  FrameBufferLease& operator=(const FrameBufferLease&) = delete;
 private:
  FrameBufferState<Sprite>& state_;
  Sprite*& published_;
  bool (*create_)(Sprite&,void*);
  void* context_;
  bool owns_ = false;
};
}  // namespace ui
