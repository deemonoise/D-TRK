#pragma once
#include <stdint.h>
#include "hw/input.h"
#include "hw/lgfx_config.h"
#include "model.h"
#include "onset.h"
#include "param_list.h"
#include "theme.h"
#include "touch.h"

namespace ui {

class App;

// SAMPLE page of InstScreen: waveform with markers (Start green, End red, Loop yellow, slices
// cyan), zoom, toolbar (< > - + CHOP CLR) and the sample rows. Marker row: click = edit, turn =
// move the selected marker 1 px of the zoom, Shift+turn = to the next / previous transient.
// Touch: tap = nearest marker, drag a marker = move it (Shift: snap to a transient), drag elsewhere
// = scroll (zoomed), long press = new slice (Shift: at the nearest transient). CLR: tap = remove the
// selected slice, long press = all slices.
class SampleEditor {
 public:
  static constexpr int kWaveH = 148;
  static constexpr int kToolH = 24;
  static constexpr int kListRows = 2;  // (page area 220 - kWaveH - kToolH) / ParamList::kRowH

  explicit SampleEditor(App& app, int y = kAreaY + 52);
  void bind(int instr);         // instrument index; resets zoom when the sample data changes
  void enter(bool bar);         // page shown: the cursor on the page bar, else the first row
  bool barSelected() const { return list_.barSelected(); }
  int onInput(const hw::InputEvent& ev);  // -1 / +1: turned past the first / last row
  void onTouch(const TouchEvent& ev);
  void draw(LGFX_Sprite& s, int y);
  bool editing() const { return list_.editing(); }
  void leaveEdit() { list_.setEdit(false); }
  uint8_t previewNote();        // NOTE mode with a slice selected: root + slice, else C4
  // Track button k (0..7): plays slice (selected slice, or the first) + k to its end, any slice mode.
  void playSlice(int k);

 private:
  static constexpr int kMarkerRow = 0, kSampleRow = 1, kRoot = 2, kStart = 3, kEnd = 4, kLoop = 5,
                       kLoopStart = 6, kReverse = 7, kSlices = 8, kChop = 9, kAmount = 10, kRows = 11;
  static constexpr int kS = 0, kE = 1, kL = 2, kSlice0 = 3;  // marker ids, slice i = kSlice0 + i
  static constexpr int kMarkers = kSlice0 + mt::kMaxSlices;
  static constexpr int kGrab = 16;      // px: tap / drag reach of a marker
  static constexpr int kLabelGap = 24;  // px between slice numbers
  static constexpr int kToolW = kScreenW / 6;
  enum Tool : int { kPrev, kNext, kZoomOut, kZoomIn, kChopBtn, kClr };
  enum class Drag : uint8_t { None, Move, Scroll };

  mt::Instrument& inst();
  int bankIndex();  // of inst().sample, -1 if absent or no bank
  bool sampleMissing();
  void sync();      // sample data / length changed: reset the view; keep sel_ valid
  bool visible(int id);
  uint32_t markerFrame(int id);
  uint16_t markerFrac(int id);  // position as a fraction of the whole sample
  void region(uint32_t& from, uint32_t& to);  // Start..End frames, as Synth::startSample
  // dir: -1 / +1 = the fraction moves at least one unit that way (encoder at full zoom).
  void setMarkerFrame(int id, uint32_t f, int dir = 0);
  int stepMarker(int dir);  // previous / next visible marker by frame order, sel_ if none
  int nearestMarker(int x);  // visible marker within kGrab px of x, -1 if none
  void selectMarker(int id);  // also selects the marker row
  int zMax() const;
  uint32_t viewLen() const;
  uint32_t gridFrame(uint32_t g) const;  // first frame of zoom grid column g
  uint32_t maxCol() const;                // last grid column the view may start on
  void setCol(int64_t g);                 // view start on grid column g, clamped
  void setView(int64_t start);  // clamped, snapped to the grid column holding start
  void setZoom(int z);          // centred on the selected marker
  void showFrame(uint32_t f);   // scroll so that f is on screen
  uint32_t frameAt(int x) const;
  int xOf(uint32_t f) const;  // may be off screen
  uint32_t snap(uint32_t f);  // nearest onset, f if none
  void ensureOnsets();
  void updateWave();
  void drawWave(LGFX_Sprite& s, int y);
  void drawTools(LGFX_Sprite& s, int y);
  void tool(int t, bool longPress);

  App& app_;
  Param rows_[kRows];
  ParamList list_;
  int y_;  // page top, from draw()
  int instr_ = -1;
  int sel_ = kS;
  int zoom_ = 0;
  uint32_t viewCol_ = 0;  // zoom grid column the view starts on (see gridFrame())
  uint16_t dragId_ = 0;
  Drag drag_ = Drag::None;
  // Current sample data (sync()).
  const int16_t* data_ = nullptr;
  uint32_t frames_ = 0, rate_ = 0, gen_ = 0;
  // Waveform cache: per screen column min / max of the view, int8; min > max = no data.
  int8_t waveMin_[kScreenW] = {0};
  int8_t waveMax_[kScreenW] = {0};
  const int16_t* waveData_ = nullptr;
  uint32_t waveFrames_ = 0, waveGen_ = 0, waveCol_ = 0, waveLen_ = 0;
  // Transients of the current data, lazily (ensureOnsets()).
  mt::Onset onsets_[mt::kMaxOnsets];
  int nOnsets_ = 0;
  const int16_t* onsData_ = nullptr;
  uint32_t onsFrames_ = 0, onsGen_ = 0;
};

}  // namespace ui
