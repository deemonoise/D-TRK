#pragma once
#include "wt_make.h"

namespace mt {
enum class WtEditMode : uint8_t { Wave, Morph };

// No ownership/allocation. Both buffers contain kWtSrcSamples int16 samples.
class WtEditor {
 public:
  void attach(int16_t* live, int16_t* undo);
  void detach();
  bool ready() const { return data_ && undo_; }
  void newWave();
  // Call after loading every sample into data(); preserves exact 16-bit source.
  void loaded();
  int16_t* data() { return data_; }
  const int16_t* data() const { return data_; }
  const int16_t* frame(int f) const;
  const int16_t* frame() const { return frame(selected_); }
  int selected() const { return selected_; }
  bool select(int f);
  WtEditMode mode() const { return mode_; }
  // Morph -> Wave flattens to current frame; caller must obtain user confirmation.
  bool setMode(WtEditMode mode);
  bool anchor(int f) const;
  bool addAnchor();
  bool copyAnchor(int from, int to); // Refuses occupied target.
  bool deleteAnchor();              // Endpoints cannot be deleted.
  bool shape(WtShape shape);
  bool smooth();
  bool beginStroke();
  // Sample x = 0..255, signed amplitude y. Clamps x; one stroke is one Undo.
  bool drawLine(int x0, int16_t y0, int x1, int16_t y1);
  void endStroke();
  bool undo();
  bool canUndo() const { return undoValid_; }
  bool dirty() const { return dirty_; }
 private:
  void snapshot();
  void rebuild();
  void changed();
  int16_t* data_ = nullptr;
  int16_t* undo_ = nullptr;
  uint64_t anchors_ = 0, oldAnchors_ = 0;
  WtEditMode mode_ = WtEditMode::Wave, oldMode_ = WtEditMode::Wave;
  uint8_t selected_ = 0, oldSelected_ = 0;
  bool dirty_ = false, oldDirty_ = false, undoValid_ = false, stroke_ = false, strokeChanged_ = false;
};
} // namespace mt
