#include "rec_dialog.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "engine/engine.h"
#include "link/rec_client.h"
#include "rec.h"
#include "storage/remote_fs.h"

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
                        src_ = want_ = s;
                        waitPlay_ = false;  // only RESAMPLE waits for Play
                        hold_.clear();
                        pendingSet_ = true;
                        edited();
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
                      edited();
                    },
                    [this] { return src_ == RecSource::Resample; }};
  params_[kThresh] = {"Thresh",
                      [this](char* o, int n) {
                        if (thresh_ == mt::kRecThreshOff) snprintf(o, n, "OFF");
                        else snprintf(o, n, "%d dB", thresh_);
                      },
                      [this](int d) {
                        if (taking_) return;
                        const int8_t v = mt::recThreshStep(thresh_, d);
                        if (v == thresh_) return;
                        thresh_ = v;
                        edited();
                      },
                      [this] { return taking_; }};
  params_[kMon] = {"Mon", [this](char* o, int n) { snprintf(o, n, "%s", mon_ ? "ON" : "OFF"); },
                   [this](int d) {
                     if (mon_ == (d > 0)) return;
                     mon_ = d > 0;
                     pendingSet_ = true;
                     edited();
                   }};
  params_[kCancel] = {"Cancel", [this](char* o, int n) { snprintf(o, n, "%s", mt::recCancelText(btnState())); },
                      nullptr};
  list_.setParams(params_, kRows);
  list_.setVisibleRows(kRows);  // all of them: the START / STOP button below
}

void RecDialog::reset() {
  taking_ = false;
  warned_ = false;
  waitPlay_ = false;
  adoptUntil_ = 0;
  state_ = RecState::Idle;
  frames_ = clips_ = left_ = 0;
  number_ = 0;
  hold_.clear();
  src_ = want_;
  codec_ = true;  // until the board says otherwise
  list_.setEdit(false);
}

bool RecDialog::open(int instr, std::function<void()> onSaved) {
  if (taking_ || adoptUntil_) {
    app_.toast("REC BUSY");
    return false;
  }
  mode_ = Mode::Sample;
  instr_ = instr;
  onSaved_ = std::move(onSaved);
  reset();
  list_.setSel(kSource);
  open_ = true;
  send();  // the codec and the meter follow the source from now on
  nextPoll_ = millis() + kPollMs;
  app_.invalidate();
  return true;
}

bool RecDialog::openSd() {
  if ((taking_ || adoptUntil_) && mode_ != Mode::Sd) {
    app_.toast("REC BUSY");
    return false;
  }
  mode_ = Mode::Sd;
  onSaved_ = nullptr;
  list_.setEdit(false);
  list_.setSel(kSource);
  if (taking_ || adoptUntil_) {  // the take recording in the background: its window again
    open_ = true;
    app_.invalidate();
    return true;
  }
  reset();
  // The card's free space for the header; it also warms the board's free-space count (slow the first
  // time on a big FAT32 card), so that RecStart answers in time.
  uint32_t total = 0, freeMb = 0;
  freeMb_ = storage::remoteSpace(total, freeMb) ? static_cast<int32_t>(freeMb) : -1;
  open_ = true;
  send();
  nextPoll_ = millis() + kPollMs;
  app_.invalidate();
  return true;
}

void RecDialog::hide() {
  if (!open_) return;
  if (!taking_ && !adoptUntil_) {
    closeWindow();
    return;
  }
  open_ = false;  // recording on: App's poll keeps the board's keepalive
  app_.invalidate();
}

mt::RecPrefs RecDialog::prefs() const {
  mt::RecPrefs p;
  p.src = want_;
  p.micGain = static_cast<int8_t>(gains_[static_cast<int>(RecSource::Mic)]);
  p.lineGain = static_cast<int8_t>(gains_[static_cast<int>(RecSource::Line)]);
  p.thresh = thresh_;
  p.mon = mon_;
  return p;
}

void RecDialog::setPrefs(const mt::RecPrefs& p) {
  want_ = src_ = p.src;
  gains_[static_cast<int>(RecSource::Mic)] = mt::recGainClamp(RecSource::Mic, p.micGain);
  gains_[static_cast<int>(RecSource::Line)] = mt::recGainClamp(RecSource::Line, p.lineGain);
  thresh_ = p.thresh;
  mon_ = p.mon;
}

void RecDialog::edited() { app_.settingsChanged(); }

uint32_t RecDialog::leftFrames() const {
  if (taking_) return left_;
  if (freeMb_ < 0) return 0;
  return mt::recSdLimitFrames(static_cast<uint64_t>(freeMb_) << 20, src_ == RecSource::Mic ? 1 : 2);
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
  const RecState st =
      rep.state <= static_cast<uint8_t>(RecState::Saving) ? static_cast<RecState>(rep.state) : RecState::Idle;
  if (!taking_ && adoptUntil_) {  // a Sd start without its reply: the board answers in order, so this says
    adoptUntil_ = 0;
    if (st == RecState::Armed || st == RecState::Rec) {
      taking_ = true;  // the take is on: its number comes with the stop's reply
      warned_ = false;
      number_ = 0;
      frames_ = clips_ = 0;
    } else {
      app_.toast(adoptMsg_);
    }
    app_.invalidate();
  }
  if (!taking_) {
    state_ = RecState::Idle;
    return;
  }
  state_ = st;
  frames_ = rep.frames;
  left_ = rep.left;
  clips_ += rep.clips;
  const RecResult r = static_cast<RecResult>(rep.result);
  if (!warned_ && (r == RecResult::CardSlow || r == RecResult::WriteFail)) {
    warned_ = true;
    app_.toast(mt::recResultText(r));
  }
  if (state_ == RecState::Saving) stop();  // the take ended itself (5 min / 4 GB / card space, full ring, card error)
  else if (state_ == RecState::Idle) {     // the board sealed it without us (keepalive lost)
    taking_ = false;
    if (mode_ == Mode::Sd && number_) {
      char msg[32];
      snprintf(msg, sizeof msg, "REC%u.WAV KEPT ON CARD", static_cast<unsigned>(number_));
      app_.toast(msg);
    } else {
      app_.toast(mode_ == Mode::Sd ? "REC KEPT ON CARD" : "REC ENDED");
    }
    app_.invalidate();
  }
}

void RecDialog::poll() {
  if (!open_ && !taking_ && !adoptUntil_) return;
  if (pendingSet_ && open_) send();
  if (waitPlay_ && app_.status().playing) onPlay();  // started elsewhere (MIDI start): late, but a take
  const uint32_t now = millis();
  if (adoptUntil_ && static_cast<int32_t>(now - adoptUntil_) >= 0) {  // no answer at all
    adoptUntil_ = 0;
    app_.toast("NO SYNTH");
    app_.invalidate();
  }
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
  const int8_t g = static_cast<int8_t>(mt::recGainClamp(src_, gain()));
  uint16_t n = 0;
  bool link = false;
  RecResult r;
  if (mode_ == Mode::Sd) {
    r = audio::recStartSd(static_cast<uint8_t>(src_), g, thresh_, mon_, n, &link);
    // No reply (the board may still be counting a big card's free space) or BUSY to a retry of the
    // start it took: the next polls tell whether the take is on.
    if (!link || r == RecResult::Busy) {
      adoptUntil_ = (millis() + kAdoptMs) | 1;
      adoptMsg_ = link ? mt::recResultText(r) : "NO SYNTH";
      nextPoll_ = millis();
      return;
    }
  } else {
    mt::link::RecStartReq q;
    q.source = static_cast<uint8_t>(src_);
    q.gain = g;
    q.thresh = thresh_;
    q.mon = mon_ ? 1 : 0;
    q.number = static_cast<uint16_t>(mt::nextRecNumber(app_.project(), 1));
    n = q.number;
    r = audio::recStart(q, n, &link);
  }
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
  left_ = leftFrames();  // until the first poll
  state_ = thresh_ == mt::kRecThreshOff ? RecState::Rec : RecState::Armed;
  list_.setEdit(false);
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
  else if (mode_ == Mode::Sd) {
    sdEnd(rep, r);
    if (freeMb_ >= 0) {  // the card's free space less the take (no recount: slow on a big card)
      const uint64_t bytes = static_cast<uint64_t>(rep.frames) * (src_ == RecSource::Mic ? 2 : 4);
      freeMb_ -= static_cast<int32_t>((bytes >> 20) + 1);
      if (freeMb_ < 0) freeMb_ = 0;
    }
  } else if (r == RecResult::Ok || r == RecResult::CardSlow) {
    bind(rep, r == RecResult::CardSlow);
  } else if (r == RecResult::Bank) app_.toast(audio::bankResultText(static_cast<audio::BankResult>(rep.bank)));
  else app_.toast(mt::recResultText(r));
  ready();
}

void RecDialog::ready() {
  if (!open_) {  // a Sd take that ended in the background: nothing on screen
    closeWindow();
    return;
  }
  reset();  // READY: no take, the meter cleared (the source picked, the gains and Thresh kept)
  send();  // the board's meter and MON on again for the next take
  nextPoll_ = millis() + kPollMs;
  app_.invalidate();
}

void RecDialog::sdEnd(const mt::link::RecStopRep& rep, RecResult r) {
  const int number = rep.number ? rep.number : number_;
  const auto ended = rep.ended <= static_cast<uint8_t>(mt::link::RecEnd::Space) ? static_cast<mt::link::RecEnd>(rep.ended)
                                                                                : mt::link::RecEnd::Stop;
  char msg[48];
  mt::recSdToast(number, rep.frames, rep.clips, r, ended, msg, sizeof msg);
  app_.toast(msg);
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
  switch (mt::recCancelAct(btnState())) {
    case mt::RecCancel::Close: closeWindow(); return;
    case mt::RecCancel::Unwait: waitPlay_ = false; break;
    case mt::RecCancel::Drop: {
      // The board answers in order: a reply to RecCancel comes after the start's, so a start not yet
      // answered (STARTING) is dropped too; no reply to it then: the poll says first.
      const RecResult r = audio::recCancel();
      if (r == RecResult::Busy || (adoptUntil_ && !taking_ && r != RecResult::Ok)) {
        app_.toast("BUSY");
        return;
      }
      app_.toast("REC CANCELLED");
      ready();  // RecCancel stopped the board's meter and MON: on again
      return;
    }
    case mt::RecCancel::None: return;  // SAVING
  }
  list_.setEdit(false);
  app_.invalidate();
}

void RecDialog::detach() {
  if (mode_ == Mode::Sd || !open_) return;
  if (taking_) app_.toast("REC KEPT ON CARD");
  open_ = false;
  taking_ = false;
}

void RecDialog::closeWindow() {
  if (open_ && !taking_) audio::recCancel();  // the meter and MON stop
  open_ = false;
  taking_ = false;
  waitPlay_ = false;
  adoptUntil_ = 0;
  app_.invalidate();
}

mt::RecBtn RecDialog::btnState() const {
  return mt::recBtnState(taking_, adoptUntil_ != 0, state_, thresh_ != mt::kRecThreshOff, waitPlay_);
}

mt::RecRect RecDialog::btnRect() {
  return mt::recBtnRect(kScreenW, kAreaY + kHeaderH + kRows * ParamList::kRowH, kAreaY + kAreaH, kBtnMargin);
}

void RecDialog::toggle() {
  switch (mt::recTap(btnState(), src_, app_.status().playing)) {
    case mt::RecTap::Start: start(); break;  // START NOW, or ARM with a Thresh
    case mt::RecTap::WaitPlay:               // RESAMPLE, stopped: Play starts it (onPlay)
      waitPlay_ = true;
      list_.setEdit(false);
      break;
    case mt::RecTap::Unwait: waitPlay_ = false; break;
    case mt::RecTap::Stop: stop(); break;  // ARMED: the arm ends; REC: the take is saved
    case mt::RecTap::None: return;         // STARTING / SAVING
  }
  app_.invalidate();
}

void RecDialog::onPlay() {
  if (!waitPlay_ || !open_ || taking_ || adoptUntil_) return;
  waitPlay_ = false;
  start();  // its toast when it fails; the transport starts anyway
  app_.invalidate();
}

bool RecDialog::action(int row) {
  if (row != kCancel) return false;
  cancel();
  app_.invalidate();
  return true;
}

void RecDialog::aTap() {
  if (open_ && mt::recBtnLive(btnState())) toggle();
}

void RecDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  if (ev.type == InputType::EncLong) {  // as the big button: start / arm, or stop (saved)
    toggle();
    return;
  }
  if (ev.type == InputType::EncClick && !list_.editing() && action(list_.sel())) return;
  list_.onInput(ev);
}

void RecDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (ev.type == TouchType::Tap) {
    if (btnRect().contains(ev.x, ev.y)) {  // Tap comes on the release
      if (mt::recBtnLive(btnState())) toggle();
      return;
    }
    const int r = list_.rowAt(ev.y);
    if (r == kCancel) {
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
  if (mode_ == Mode::Sd) snprintf(line, sizeof line, "REC -> SD");
  else snprintf(line, sizeof line, "REC -> INS%d", instr_ + 1);
  s.setTextColor(kText);
  s.drawString(line, ParamList::kLabelX, y0 + 2);
  const char* st = "READY";
  uint16_t stc = kDim;
  if (adoptUntil_) {
    st = "STARTING";
    stc = kCursor;
  } else if (waitPlay_ && !taking_) {
    st = (now / kBlinkMs) & 1 ? "ARMED: PLAY" : "";
    stc = kCursor;
  }
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
  char t[12], lim[16];
  mt::recFmtTime(taking_ ? frames_ : 0, t, sizeof t);
  if (mode_ == Mode::Sd) {  // the time left: the take's, or the card's space before it
    const uint32_t left = leftFrames();
    if (left) mt::recFmtClock(left / mt::kRecRate, false, lim, sizeof lim);
    else snprintf(lim, sizeof lim, "--");
    snprintf(line, sizeof line, "%s  LEFT %s", t, lim);
  } else {
    mt::recFmtTime(mt::kRecMaxFrames, lim, sizeof lim);
    snprintf(line, sizeof line, "%s / %s", t, lim);
  }
  s.setTextColor(kText);
  s.drawString(line, ParamList::kLabelX, my + kMeterH + 4);
  if (taking_ && clips_) {
    snprintf(line, sizeof line, "%lu CLIP%s", static_cast<unsigned long>(clips_), clips_ == 1 ? "" : "S");
    s.setTextColor(kRed);
    s.drawString(line, kMeterX1 + 8, my + kMeterH + 4);
  }

  list_.draw(s, y0 + kHeaderH);
  drawBtn(s, y0, now);
}

// The big START / STOP button: READY a red outline, REC filled red with the time, ARMED blinking in
// step with the header, STARTING / SAVING grey. The biggest text scale that fits.
void RecDialog::drawBtn(LGFX_Sprite& s, int y0, uint32_t now) {
  mt::RecRect r = btnRect();
  r.y += y0 - kAreaY;
  if (r.h <= 0) return;
  const mt::RecBtn b = btnState();
  const bool live = mt::recBtnLive(b);
  const bool filled = b == mt::RecBtn::Stop || (mt::recBtnBlinks(b) && ((now / kBlinkMs) & 1));
  const uint16_t edge = live ? kRed : kDim;
  const uint16_t fg = filled ? kText : edge;
  s.fillRect(r.x, r.y, r.w, r.h, filled ? kRed : kBg);
  s.drawRect(r.x, r.y, r.w, r.h, edge);
  s.drawRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, edge);
  char text[24];
  mt::recBtnText(b, frames_, text, sizeof text);
  const int icon = live ? 2 : 0;  // cells: the dot / square and a gap
  const int cols = static_cast<int>(strlen(text)) + icon;
  const int sc = mt::recBtnScale(cols, kCharW, kCharH, r.w - 16, r.h - 8, 3);
  const int cw = kCharW * sc, ch = kCharH * sc;
  int x = r.x + (r.w - cols * cw) / 2;
  const int y = r.y + (r.h - ch) / 2, cy = r.y + r.h / 2;
  if (icon) {
    const int half = 5 * sc;
    if (b == mt::RecBtn::Stop) s.fillRect(x + 6 * sc - half, cy - half, 2 * half, 2 * half, fg);
    else s.fillCircle(x + 6 * sc, cy, half, fg);
    x += icon * cw;
  }
  s.setTextColor(fg);
  s.setTextSize(sc);
  s.drawString(text, x, y);
  s.setTextSize(1);
}

}  // namespace ui
