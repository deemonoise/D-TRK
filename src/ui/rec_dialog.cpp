#include "rec_dialog.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "engine/engine.h"
#include "link/rec_client.h"
#include "rec.h"

namespace ui {

using mt::RecSource;
using mt::RecState;
using mt::link::RecResult;

namespace {
const char* const kSourceNames[] = {"MIC", "LINE", "RESAMPLE"};
constexpr int kMeterX0 = ParamList::kLabelX, kMeterX1 = 392, kMeterH = 12;

void saving(uint32_t done, uint32_t total, void* ctx) { static_cast<App*>(ctx)->showProgress("SAVING", done, total); }
}  // namespace

RecDialog::RecDialog(App& app) : app_(app) {
  params_[kSource] = {"Source",
                      [this](char* o, int n) {
                        if (codec_) snprintf(o, n, "%s", kSourceNames[static_cast<int>(src_)]);
                        else snprintf(o, n, "RESAMPLE  (NO SHIELD)");
                      },
                      [this](int d) {
                        if (taking_) return;
                        const RecSource s = mt::recSourceStep(src_, d, codec_);
                        if (s == src_) return;
                        src_ = s;
                        hold_.clear();
                        pendingSet_ = true;
                      },
                      [this] { return taking_ || !codec_; }};
  params_[kGain] = {"Gain",
                    [this](char* o, int n) {
                      if (src_ == RecSource::Mic) snprintf(o, n, "+%d dB", gain());
                      else if (src_ == RecSource::Line) snprintf(o, n, "%d", gain());
                      else snprintf(o, n, "-");
                    },
                    [this](int d) {
                      int& g = gains_[static_cast<int>(src_)];
                      const int v = mt::recGainClamp(src_, g + d);
                      if (v == g) return;
                      g = v;
                      pendingSet_ = true;
                    },
                    [this] { return src_ == RecSource::Resample; }};
  params_[kThresh] = {"Thresh",
                      [this](char* o, int n) {
                        if (thresh_ == mt::kRecThreshOff) snprintf(o, n, "OFF");
                        else snprintf(o, n, "%d dB", thresh_);
                      },
                      [this](int d) {
                        if (!taking_) thresh_ = mt::recThreshStep(thresh_, d);
                      },
                      [this] { return taking_; }};
  params_[kMon] = {"Mon", [this](char* o, int n) { snprintf(o, n, "%s", mon_ ? "ON" : "OFF"); },
                   [this](int d) {
                     if (mon_ == (d > 0)) return;
                     mon_ = d > 0;
                     pendingSet_ = true;
                   }};
  params_[kRec] = {"REC",
                   [this](char* o, int n) {
                     snprintf(o, n, "%s",
                              taking_ ? "SAVE THE TAKE" : (thresh_ == mt::kRecThreshOff ? "START NOW" : "ARM"));
                   },
                   nullptr};
  params_[kCancel] = {"Cancel", [](char* o, int n) { snprintf(o, n, "DROP THE TAKE"); }, nullptr};
  list_.setParams(params_, kRows);
  list_.setVisibleRows((kAreaH - kHeaderH) / ParamList::kRowH);
}

void RecDialog::open(int instr, std::function<void()> onSaved) {
  instr_ = instr;
  onSaved_ = std::move(onSaved);
  taking_ = false;
  warned_ = false;
  state_ = RecState::Idle;
  frames_ = clips_ = 0;
  number_ = 0;
  hold_.clear();
  codec_ = true;  // until the board says otherwise
  list_.setEdit(false);
  list_.setSel(kSource);
  open_ = true;
  send();  // the codec and the meter follow the source from now on
  nextPoll_ = millis() + kPollMs;
  app_.invalidate();
}

void RecDialog::send() {
  pendingSet_ = false;
  for (int tries = 0; tries < 2; ++tries) {
    mt::link::RecLevelRep rep;
    bool link = false;
    const RecResult r = audio::recSet(static_cast<int8_t>(mt::recGainClamp(src_, gain())), mon_,
                                      static_cast<uint8_t>(src_), &rep, &link);
    if (!link) {
      if (!taking_) app_.toast("NO SYNTH");
      return;
    }
    codec_ = rep.codec != 0;
    // No shield: only RESAMPLE has a signal.
    if ((r == RecResult::NoCodec || !codec_) && src_ != RecSource::Resample && !taking_) {
      codec_ = false;
      src_ = RecSource::Resample;
      continue;
    }
    return;
  }
}

void RecDialog::level(const mt::link::RecLevelRep& rep, uint32_t now) {
  codec_ = rep.codec != 0;
  if (!codec_ && src_ != RecSource::Resample && !taking_) {  // the shield stopped answering
    src_ = RecSource::Resample;
    pendingSet_ = true;
  }
  hold_.feed(rep.peak, now);
  if (!taking_) {
    state_ = RecState::Idle;
    return;
  }
  state_ = rep.state <= static_cast<uint8_t>(RecState::Saving) ? static_cast<RecState>(rep.state) : RecState::Idle;
  frames_ = rep.frames;
  clips_ += rep.clips;
  const RecResult r = static_cast<RecResult>(rep.result);
  if (!warned_ && (r == RecResult::CardSlow || r == RecResult::WriteFail)) {
    warned_ = true;
    app_.toast(mt::recResultText(r));
  }
  if (state_ == RecState::Saving) stop();  // the take ended itself (5 min, full ring, card error)
  else if (state_ == RecState::Idle) {     // the board sealed it without us (keepalive lost)
    taking_ = false;
    app_.toast("REC ENDED");
  }
}

void RecDialog::poll() {
  if (!open_) return;
  if (pendingSet_) send();
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextPoll_) < 0) return;
  mt::link::RecLevelRep rep;
  if (audio::recLevel(rep)) {
    nextPoll_ = now + kPollMs;
    level(rep, millis());
  } else {
    nextPoll_ = now + kRetryMs;
  }
}

void RecDialog::start() {
  mt::link::RecStartReq q;
  q.source = static_cast<uint8_t>(src_);
  q.gain = static_cast<int8_t>(mt::recGainClamp(src_, gain()));
  q.thresh = thresh_;
  q.mon = mon_ ? 1 : 0;
  q.number = static_cast<uint16_t>(mt::nextRecNumber(app_.project(), 1));
  uint16_t n = q.number;
  bool link = false;
  const RecResult r = audio::recStart(q, n, &link);
  if (!link) {
    app_.toast("NO SYNTH");
    return;
  }
  if (r != RecResult::Ok) {
    app_.toast(mt::recResultText(r));
    return;
  }
  taking_ = true;
  warned_ = false;
  number_ = n;
  frames_ = clips_ = 0;
  state_ = q.thresh == mt::kRecThreshOff ? RecState::Rec : RecState::Armed;
  list_.setEdit(false);
  list_.setSel(kRec);
  nextPoll_ = millis() + kPollMs;
}

void RecDialog::stop() {
  if (!taking_) {
    closeWindow();
    return;
  }
  list_.setEdit(false);
  state_ = RecState::Saving;
  mt::link::RecStopRep rep;
  bool link = false;
  const RecResult r = audio::recStop(rep, saving, &app_, &link);
  app_.endProgress();
  taking_ = false;
  state_ = RecState::Idle;
  if (!link) app_.toast("NO SYNTH");
  else if (r == RecResult::Ok || r == RecResult::CardSlow) {
    bind(rep, r == RecResult::CardSlow);
    return;
  } else if (r == RecResult::Bank) app_.toast(audio::bankResultText(static_cast<audio::BankResult>(rep.bank)));
  else app_.toast(mt::recResultText(r));
  closeWindow();
}

void RecDialog::bind(const mt::link::RecStopRep& rep, bool cardSlow) {
  const int number = rep.number ? rep.number : number_;
  mt::Project& p = app_.project();
  engine::lockProject();
  // RECn of the file when the project has no such sample, else the next free RECk.
  const int k = mt::nextRecNumber(p, number);
  char name[mt::kSampleNameMax + 1];
  mt::recName(k, name);
  const int idx = k > 0 ? mt::recBind(p, instr_, name, rep.crc, rep.frames, 60) : -1;
  engine::unlockProject();
  closeWindow();
  if (idx < 0) {
    app_.toast("SAMPLE LIST FULL");
    return;
  }
  audio::bankViewStale();
  app_.markDirty();
  char msg[48];
  mt::recToast(k, rep.frames, rep.clips, cardSlow, msg, sizeof msg);
  app_.toast(msg);
  if (onSaved_) onSaved_();
}

void RecDialog::cancel() {
  if (!open_) return;
  if (taking_) {
    const RecResult r = audio::recCancel();
    if (r == RecResult::Busy) {
      app_.toast("BUSY");
      return;
    }
    taking_ = false;
    app_.toast("REC CANCELLED");
  }
  closeWindow();
}

void RecDialog::detach() {
  if (!open_) return;
  if (taking_) app_.toast("REC KEPT ON CARD");
  open_ = false;
  taking_ = false;
}

void RecDialog::closeWindow() {
  if (open_ && !taking_) audio::recCancel();  // the meter and MON stop
  open_ = false;
  taking_ = false;
  app_.invalidate();
}

bool RecDialog::action(int row) {
  if (row == kRec) {
    if (taking_) stop();
    else start();
  } else if (row == kCancel) {
    cancel();
  } else {
    return false;
  }
  app_.invalidate();
  return true;
}

void RecDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  if (ev.type == InputType::EncLong) {  // as leaving: a take is kept
    stop();
    return;
  }
  if (ev.type == InputType::EncClick && !list_.editing() && action(list_.sel())) return;
  list_.onInput(ev);
}

void RecDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (ev.type == TouchType::Tap) {
    const int r = list_.rowAt(ev.y);
    if (r == kRec || r == kCancel) {
      list_.setSel(r);
      action(r);
      return;
    }
  }
  list_.onTouch(ev);
}

void RecDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  const uint32_t now = millis();
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  char line[48];
  // Title and state.
  snprintf(line, sizeof line, "REC -> INS%d", instr_ + 1);
  s.setTextColor(kText);
  s.drawString(line, ParamList::kLabelX, y0 + 2);
  const char* st = "READY";
  uint16_t stc = kDim;
  switch (taking_ ? state_ : RecState::Idle) {
    case RecState::Armed:
      st = (now / kBlinkMs) & 1 ? "ARMED" : "";
      stc = kCursor;
      break;
    case RecState::Rec:
      st = "REC";
      stc = kRed;
      break;
    case RecState::Saving: st = "SAVING"; break;
    default: break;
  }
  if (taking_ && number_) {
    snprintf(line, sizeof line, "%s  REC%u", st, static_cast<unsigned>(number_));
    st = line;
  }
  s.setTextColor(stc);
  s.drawString(st, kScreenW - ParamList::kLabelX - static_cast<int>(strlen(st)) * kCharW, y0 + 2);

  // Meter: -60..0 dB, the held peak as a tick, red at -1 dB and up.
  const int my = y0 + 2 + kCharH + 4, mw = kMeterX1 - kMeterX0;
  const uint16_t pk = hold_.value(now), cur = hold_.last;
  s.drawRect(kMeterX0, my, mw, kMeterH, kDim);
  const int curPx = mt::recMeterPx(cur, mw - 2);
  if (curPx > 0) s.fillRect(kMeterX0 + 1, my + 1, curPx, kMeterH - 2, mt::recPeakRed(cur) ? kRed : kGreen);
  const int pkPx = mt::recMeterPx(pk, mw - 2);
  if (pkPx > 0) s.fillRect(kMeterX0 + pkPx - 1, my + 1, 2, kMeterH - 2, mt::recPeakRed(pk) ? kRed : kText);
  if (pk) snprintf(line, sizeof line, "%d dB", mt::recPeakDb(pk));
  else snprintf(line, sizeof line, "-inf");
  s.setTextColor(mt::recPeakRed(pk) ? kRed : kText);
  s.drawString(line, kMeterX1 + 8, my - 2);

  // Time / limit, clips.
  char t[12], lim[12];
  mt::recFmtTime(taking_ ? frames_ : 0, t, sizeof t);
  mt::recFmtTime(mt::kRecMaxFrames, lim, sizeof lim);
  snprintf(line, sizeof line, "%s / %s", t, lim);
  s.setTextColor(kText);
  s.drawString(line, ParamList::kLabelX, my + kMeterH + 4);
  if (taking_ && clips_) {
    snprintf(line, sizeof line, "%lu CLIP%s", static_cast<unsigned long>(clips_), clips_ == 1 ? "" : "S");
    s.setTextColor(kRed);
    s.drawString(line, kMeterX1 + 8, my + kMeterH + 4);
  }

  params_[kRec].label = taking_ ? "STOP" : "REC";
  list_.draw(s, y0 + kHeaderH);
}

}  // namespace ui
