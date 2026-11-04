#include "sample_extract_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "engine/engine.h"
#include "link/bank_client.h"
#include "render_ui.h"
#include "storage/storage.h"

namespace ui {
namespace {
bool progress(uint32_t done, uint32_t total, void* app) {
  return App::labeledProgress("EXTRACT", done, total, app);
}
bool stretchProgress(uint32_t done, uint32_t total, void* app) {
  return App::labeledProgress("TIME STRETCH", done, total, app);
}
bool tempoProgress(uint32_t done, uint32_t total, void* app) {
  return App::labeledProgress("DETECT BPM", done, total, app);
}
int bpmClamp(int bpm) { return bpm<200 ? 200 : bpm>4800 ? 4800 : bpm; }
void bpmText(char* out,int n,int bpm) { snprintf(out,n,"%d.%d",bpm/10,bpm%10); }
}  // namespace

SampleExtractDialog::SampleExtractDialog(App& app) : app_(app) {
  params_[kName] = {"Name", [this](char* out, int n) { snprintf(out, n, "%s", name_); }, nullptr};
  params_[kNormalize] = {"Normalize", [this](char* out, int n) { snprintf(out, n, "%s", normalize_ ? "-1 DBFS" : "OFF"); },
                         [this](int delta) { normalize_ = delta > 0; }};
  list_.setParams(params_, 2);
  list_.setVisibleRows(actions_.listRows(kHeaderH));
}

void SampleExtractDialog::open(int instrument, int index, uint32_t from, uint32_t to, uint32_t rate) {
  stretch_=false;detected_=false;tempo_={};
  normalize_ = false;
  params_[kNormalize] = {"Normalize", [this](char* out,int n){snprintf(out,n,"%s",normalize_?"-1 DBFS":"OFF");},
                         [this](int delta){normalize_=delta>0;}};
  list_.setParams(params_,2);
  target_ = mt::SampleExtractTarget{};
  target_.instrument = instrument;
  target_.index = index;
  target_.from = from;
  target_.to = to;
  engine::lockProject();
  const mt::Project& p = app_.project();
  if (index >= 0 && index < p.sampleCount) target_.source = p.samples[index];
  const bool named = mt::sampleExtractName(p, target_.source.name, name_);
  const mt::SampleExtractResult result = mt::sampleExtractCheck(p, target_, name_);
  engine::unlockProject();
  if (!named || result != mt::SampleExtractResult::Ok) {
    resultToast(named ? result : mt::SampleExtractResult::NameTaken);
    return;
  }
  rate_ = rate ? rate : 44100;
  list_.setEdit(false);
  list_.setSel(kName);
  actions_.reset(list_);
  open_ = true;
}

void SampleExtractDialog::openStretch(int instrument,int index,uint32_t from,uint32_t to,uint32_t rate) {
  open(instrument,index,from,to,rate);
  if (!open_) return;
  stretch_=true;
  sourceBpm10_=targetBpm10_=bpmClamp(app_.project().bpm*10);
  params_[kSourceBpm]={"Source BPM",[this](char* out,int n){bpmText(out,n,sourceBpm10_);},
      [this](int d){sourceBpm10_=bpmClamp(sourceBpm10_+d);detected_=false;}};
  params_[kTargetBpm]={"Target BPM",[this](char* out,int n){bpmText(out,n,targetBpm10_);},
      [this](int d){targetBpm10_=bpmClamp(targetBpm10_+d);}};
  params_[kDetect]={"Detect BPM",[this](char* out,int n){
    if (!detected_) snprintf(out,n,"MANUAL / RUN");
    else if (!tempo_.bpm10) snprintf(out,n,"UNKNOWN / RUN");
    else snprintf(out,n,"%u/1000%s",tempo_.confidence,tempo_.ambiguous?" ?":"");
  },nullptr};
  params_[kHalf]={"Use half BPM",[this](char* out,int n){if(tempo_.halfBpm10)bpmText(out,n,tempo_.halfBpm10);else snprintf(out,n,"-");},nullptr};
  params_[kDouble]={"Use double BPM",[this](char* out,int n){if(tempo_.doubleBpm10)bpmText(out,n,tempo_.doubleBpm10);else snprintf(out,n,"-");},nullptr};
  list_.setParams(params_,kRows);
}

void SampleExtractDialog::detectTempo() {
  if (!renderReady(app_)) return;
  mt::TempoEstimate estimate;
  app_.showBusy("DETECT BPM...");
  const auto result=audio::detectSampleTempo(target_.index,target_.source.crc,target_.from,target_.to,estimate,tempoProgress,&app_);
  app_.endProgress();
  if (result!=audio::BankResult::Ok) { app_.toast(audio::bankResultText(result));return; }
  tempo_=estimate;detected_=true;
  if (tempo_.bpm10) sourceBpm10_=tempo_.bpm10;
  app_.toast(tempo_.bpm10 ? "BPM ESTIMATE: CHECK HALF/DOUBLE" : "BPM UNKNOWN: SET SOURCE BPM");
  app_.invalidate();
}

bool SampleExtractDialog::rowAction(int row) {
  if (row==kName) {editName();return true;}
  if (!stretch_) return false;
  if (row==kDetect) {detectTempo();return true;}
  if (row==kHalf || row==kDouble) {
    const int bpm=row==kHalf ? tempo_.halfBpm10 : tempo_.doubleBpm10;
    if (bpm) sourceBpm10_=bpm;
    else app_.toast("DETECT BPM FIRST");
    return true;
  }
  return false;
}

void SampleExtractDialog::cancel() {
  open_ = false;
  kb_.close();
  list_.setEdit(false);
}

void SampleExtractDialog::resultToast(mt::SampleExtractResult result) {
  switch (result) {
    case mt::SampleExtractResult::Ok: return;
    case mt::SampleExtractResult::BadSource: app_.toast("SOURCE CHANGED / MISSING"); return;
    case mt::SampleExtractResult::BadName: app_.toast("BAD SAMPLE NAME"); return;
    case mt::SampleExtractResult::NameTaken: app_.toast("SAMPLE NAME EXISTS"); return;
    case mt::SampleExtractResult::Full: app_.toast("SAMPLE LIST FULL"); return;
    case mt::SampleExtractResult::Cancelled: app_.toast("CANCELLED"); return;
    case mt::SampleExtractResult::JobFailed: app_.toast("EXTRACT INCOMPLETE"); return;
  }
}

void SampleExtractDialog::create() {
  if (stretch_ && !mt::sampleStretchRatio(sourceBpm10_,targetBpm10_,target_.ratioQ16)) {
    app_.toast("STRETCH RANGE 50-200%");return;
  }
  if (!renderReady(app_)) return;
  engine::lockProject();
  const mt::SampleExtractResult ready = mt::sampleExtractCheck(app_.project(), target_, name_);
  engine::unlockProject();
  if (ready != mt::SampleExtractResult::Ok) {
    resultToast(ready);
    return;
  }
  mt::ImportOut output;
  app_.showBusy(stretch_ ? "TIME STRETCH..." : "EXTRACT...");
  const audio::BankResult result = stretch_
      ? audio::stretchSample(target_.index,target_.source.crc,target_.from,target_.to,target_.ratioQ16,output,stretchProgress,&app_)
      : audio::extractSample(target_.index,target_.source.crc,target_.from,target_.to,normalize_,output,progress,&app_);
  app_.endProgress();
  if (result != audio::BankResult::Ok) {
    app_.toast(audio::bankResultText(result));
    return;
  }
  engine::lockProject();
  const mt::SampleExtractResult committed = mt::sampleExtractCommit(app_.project(), target_, name_, result, output);
  if (committed == mt::SampleExtractResult::Ok) app_.markDirty();
  engine::unlockProject();
  if (committed != mt::SampleExtractResult::Ok) {
    resultToast(committed);
    return;
  }
  char message[40];
  snprintf(message, sizeof message, "CREATED %s", name_);
  app_.toast(message);
  audio::bankViewStale();
  cancel();
  app_.invalidate();
}

void SampleExtractDialog::editName() {
  kb_.open("SAMPLE NAME:", name_, [this](const char* text) {
    char cleaned[mt::kSampleNameMax + 1];
    if (storage::sanitize(text, cleaned)) strcpy(name_, cleaned);
    else app_.toast("BAD SAMPLE NAME");
    app_.invalidate();
  });
}

void SampleExtractDialog::onInput(const hw::InputEvent& ev) {
  if (!open_) return;
  if (kb_.isOpen()) { kb_.onInput(ev); app_.invalidate(); return; }
  if (ev.type == hw::InputType::EncLong || ev.type == hw::InputType::PlayPress) {
    cancel(); app_.invalidate(); return;
  }
  if (ev.type == hw::InputType::EncClick && !actions_.footerFocused() && rowAction(list_.sel())) return;
  const int action = actions_.input(list_, rowCount(), ev);
  if (action == 0) create();
  else if (action == 1) cancel();
}

void SampleExtractDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if (kb_.isOpen()) { kb_.onTouch(ev, app_.shift()); app_.invalidate(); return; }
  if (ev.type == TouchType::Tap && ev.y < y0_ + kAreaH - DialogActions::kH) {
    const int row=list_.rowAt(ev.y);
    if (row==kName || (stretch_ && row>=kDetect && row<kRows)) {
      actions_.focusList(list_);list_.setSel(row);rowAction(row);return;
    }
  }
  const int action = actions_.touch(list_, ev, y0_);
  if (action == 0) create();
  else if (action == 1) cancel();
}

void SampleExtractDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  if (kb_.isOpen()) { kb_.draw(s, y0); return; }
  char title[48];
  const uint32_t ms = static_cast<uint32_t>(static_cast<uint64_t>(target_.to-target_.from) * 1000 / rate_);
  if (stretch_) {
    uint32_t ratio;
    if (mt::sampleStretchRatio(sourceBpm10_,targetBpm10_,ratio))
      snprintf(title,sizeof title,"STRETCH %lu%% %lu MS",static_cast<unsigned long>((ratio*100u+32768)/65536),
               static_cast<unsigned long>((static_cast<uint64_t>(ms)*ratio+32768)/65536));
    else snprintf(title,sizeof title,"STRETCH RANGE 50-200%%");
  } else snprintf(title, sizeof title, "EXTRACT %lu MS", static_cast<unsigned long>(ms));
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  s.setTextColor(kText);
  s.drawString(title, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
  actions_.draw(s, y0, "CREATE");
}
}  // namespace ui
