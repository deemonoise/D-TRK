#include "dice_dialog.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "storage/settings.h"

namespace ui {
using namespace dice_dialog;
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int wrapi(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }

const char* const kScopeNames[] = {"ALL", "TEMPO", "SOUNDS", "PATTERNS"};
constexpr uint32_t kSeedMax = 999999;
constexpr uint8_t kRoleMask = (1u << mt::kDiceRoles) - 1;

enum : int { kMenuCancel, kMenuOverwrite };

}  // namespace

DiceDialog::DiceDialog(App& app) : app_(app) {
  params_[kGenre] = {"Group",
                     [this](char* o, int n) {
                       const char* g = mt::diceGenreName(pr_.genre);
                       snprintf(o, n, "%s", g ? g : "-");
                     },
                     [this](int d) {
                       const int g = wrapi(pr_.genre + d, mt::diceGenreCount());
                       if (g != pr_.genre) pr_.style = 0;  // the styles are the genre's
                       pr_.genre = static_cast<uint8_t>(g);
                     }};
  params_[kStyle] = {"Style",
                     [this](char* o, int n) {
                       const char* st = mt::diceStyleName(pr_.genre, pr_.style);
                       snprintf(o, n, "%s", st ? st : "-");
                     },
                     [this](int d) { pr_.style = static_cast<uint8_t>(wrapi(pr_.style + d, mt::diceStyleCount(pr_.genre))); }};
  params_[kVariant] = {"Variant", [this](char* o,int n) {
      if(pr_.variant==mt::kDiceVariantAuto) { snprintf(o,n,"AUTO"); return; }
      const auto* variant=mt::diceVariant(pr_.genre,pr_.style,pr_.variant);
      snprintf(o,n,"%02u %s",pr_.variant+1,variant ? variant->name : "-");
    }, [this](int d){pr_.variant=wrapi(pr_.variant+d,mt::kDiceVariants+1);}};
  params_[kKey]={"Key",[this](char* o,int n){snprintf(o,n,"%s",pr_.keepKey ? "KEEP" : "AUTO");},
    [this](int d){pr_.keepKey=d>0;},
    [this]{return pr_.scope==kScSounds || pr_.scope==kScPatterns || (pr_.scope==kScAll && (pr_.target || pr_.locks || pr_.change!=2));},nullptr,
    []{return "KEY CHANGES ON FULL ALL OR TEMPO ROLL";}};
  static const char* const changes[]={"DYNAMICS","BLEND","NEW"};
  params_[kTarget]={"Target",[this](char* o,int n){if(!pr_.target) snprintf(o,n,"ALL");
      else { const auto* v=mt::diceVariant(pr_.genre,pr_.style,0);
        snprintf(o,n,"T%u %s",pr_.target,v ? mt::diceTrackName(*v,pr_.target-1) : "-"); }},
    [this](int d){pr_.target=clampi(pr_.target+d,0,6);},[this]{return pr_.scope==kScTempo;}};
  params_[kChange]={"Notes",[this](char* o,int n){snprintf(o,n,"%s",changes[pr_.change]);},
    [this](int d){pr_.change=clampi(pr_.change+d,0,2);},[this]{return !patterns();}};
  params_[kForm]={"Form",[this](char* o,int n){snprintf(o,n,"%s",formName(pr_.form));},
    [this](int d){pr_.form=changeForm(pr_.form,d);},[this]{return !patterns() || pr_.target || pr_.change!=2;}};
  params_[kFeel]={"Feel",[this](char* o,int n){snprintf(o,n,"%u",pr_.feel);},
    [this](int d){pr_.feel=clampi(pr_.feel+d,0,100);},[this]{return !patterns() || pr_.change!=2;}};
  static const char* const densities[]={"SPARSE","NORMAL","BUSY"};
  params_[kDensity]={"Density",[this](char* o,int n){snprintf(o,n,"%s",densities[pr_.density]);},
    [this](int d){pr_.density=clampi(pr_.density+d,0,2);},[this]{return !patterns() || pr_.change!=2;}};
  params_[kVariety] = {"Variety", [this](char* o, int n) { snprintf(o, n, "%u", static_cast<unsigned>(pr_.variety)); },
                       [this](int d) { pr_.variety = static_cast<uint8_t>(clampi(pr_.variety + d, 0, 100)); },
                       [this] { return pr_.scope==kScTempo; }};
  params_[kLength] = {"Length",
                      [this](char* o, int n) {
                        const uint8_t l = kLens[pr_.len % kLenCount];
                        if (l) snprintf(o, n, "%u", static_cast<unsigned>(l));
                        else snprintf(o, n, "AUTO");
                      },
                      [this](int d) { pr_.len = static_cast<uint8_t>(clampi(pr_.len + d, 0, kLenCount - 1)); },
                      [this] { return !patterns() || pr_.target || pr_.change!=2 || pr_.locks; }};
  params_[kScope] = {"Scope", [this](char* o, int n) { snprintf(o, n, "%s", kScopeNames[pr_.scope % kScopes]); },
                     [this](int d) { pr_.scope = static_cast<uint8_t>(clampi(pr_.scope + d, 0, kScopes - 1)); }};
  params_[kSong] = {"Song", [this](char* o, int n) { snprintf(o, n, "%s", pr_.song ? "ON" : "OFF"); },
                    [this](int d) { pr_.song = d > 0; }, [this] { return !patterns() || pr_.target || pr_.locks || pr_.change!=2; }};
  params_[kClean] = {"Clean", [this](char* o, int n) { snprintf(o, n, "%s", pr_.clean ? "ON" : "OFF"); },
                     [this](int d) { pr_.clean = d > 0; },
                     [this] { return (pr_.scope != kScAll && pr_.scope != kScSounds) || (patterns() && pr_.change!=2); }, nullptr,
                     [] { return "CLEAN APPLIES TO SOUNDS"; }};
  params_[kSeedMode] = {"Seed mode", [this](char* o, int n) { snprintf(o, n, "%s", pr_.fixedSeed ? "FIXED" : "NEW"); },
                        [this](int d) { pr_.fixedSeed = d > 0; }};
  params_[kSeed] = {"Seed", [this](char* o, int n) { snprintf(o, n, "%lu", static_cast<unsigned long>(pr_.seed)); },
                    [this](int d) {
                      const int64_t n = static_cast<int64_t>(pr_.seed) + d;
                      pr_.seed = static_cast<uint32_t>(n < 1 ? 1 : n > kSeedMax ? kSeedMax : n);
                      pr_.fixedSeed = 1;
                    }};
  for (int role = 0; role < mt::kDiceRoles; ++role) {
    static const char* const labels[] = {"Lock T1", "Lock T2", "Lock T3", "Lock T4", "Lock T5", "Lock T6"};
    params_[kLockDrums + role] = {
        labels[role],
        [this, role](char* o, int n) { const auto* v=mt::diceVariant(pr_.genre,pr_.style,0);
          snprintf(o,n,"%s %s",pr_.locks & (1u << role) ? "ON" : "OFF",v ? mt::diceTrackName(*v,role) : "-"); },
        [this, role](int d) {
          if (d > 0) pr_.locks |= static_cast<uint8_t>(1u << role);
          else pr_.locks &= static_cast<uint8_t>(~(1u << role));
        }, [this] { return pr_.scope==kScTempo; }};
  }
  params_[kRoll] = {"Roll", nullptr, nullptr};
  params_[kClose] = {"Close", [](char* o, int n) { snprintf(o, n, "LONG PRESS"); }, nullptr};
  list_.setParams(params_, kRoll);
  list_.setEditScope([this]() -> void* { return &pr_; }, sizeof(pr_));
  list_.setVisibleRows(actions_.listRows(kHeaderH));
}

void DiceDialog::clampPrefs() {
  if (pr_.genre >= mt::diceGenreCount()) pr_.genre = 0;
  if (pr_.style >= mt::diceStyleCount(pr_.genre)) pr_.style = 0;
  if (pr_.variety > 100) pr_.variety = mt::kDiceVarietyDefault;
  if (pr_.scope >= kScopes) pr_.scope = kScAll;
  pr_.song = pr_.song ? 1 : 0;
  if (pr_.len >= kLenCount) pr_.len = 0;
  if (pr_.variant > mt::kDiceVariantAuto) pr_.variant = mt::kDiceVariantAuto;
  pr_.clean = pr_.clean ? 1 : 0;
  pr_.fixedSeed = pr_.fixedSeed ? 1 : 0;
  pr_.keepKey = pr_.keepKey ? 1 : 0;
  pr_.locks &= kRoleMask;
  if(pr_.target>6)pr_.target=0;
  if(pr_.change>2)pr_.change=2;
  pr_.form=clampForm(pr_.form);
  if(pr_.feel>100)pr_.feel=35;
  if(pr_.density>2)pr_.density=1;
  if (pr_.seed < 1 || pr_.seed > kSeedMax) pr_.seed = 1;
}

void DiceDialog::loadPrefs() {
  const Prefs d;
  pr_.genre = storage::loadSetting("dg2gen", d.genre);
  pr_.style = storage::loadSetting("dg2sty", d.style);
  pr_.variety = storage::loadSetting("dg2var", d.variety);
  pr_.scope = storage::loadSetting("dg2scp", d.scope);
  pr_.song = storage::loadSetting("dg2song", d.song);
  const uint8_t steps = storage::loadSetting("dg2len", kLens[d.len]);  // NVS keeps the steps (0 = AUTO)
  pr_.len = 0;
  for (int i = 0; i < kLenCount; ++i)
    if (kLens[i] == steps) pr_.len = static_cast<uint8_t>(i);
  pr_.variant = storage::loadSetting("dg2variant", d.variant);
  pr_.clean = storage::loadSetting("dg2clean", d.clean);
  pr_.fixedSeed = storage::loadSetting("dg2fixed", d.fixedSeed);
  pr_.keepKey = storage::loadSetting("dg4key", d.keepKey);
  pr_.locks = storage::loadSetting("dg3locks", d.locks);
  pr_.seed = static_cast<uint32_t>(storage::loadSetting("dg2seed0", d.seed & 255u)) |
             static_cast<uint32_t>(storage::loadSetting("dg2seed1", (d.seed >> 8) & 255u)) << 8 |
             static_cast<uint32_t>(storage::loadSetting("dg2seed2", (d.seed >> 16) & 255u)) << 16;
  pr_.target=storage::loadSetting("dg3target",d.target);
  pr_.change=storage::loadSetting("dg2change",d.change);
  pr_.form=storage::loadSetting("dg2form",d.form);
  pr_.feel=storage::loadSetting("dg2feel",d.feel);
  pr_.density=storage::loadSetting("dg2density",d.density);
  clampPrefs();
  saved_ = pr_;
}

void DiceDialog::savePrefs() {
  if (!(pr_ != saved_)) return;
  if (pr_.genre != saved_.genre) storage::saveSetting("dg2gen", pr_.genre);
  if (pr_.style != saved_.style) storage::saveSetting("dg2sty", pr_.style);
  if (pr_.variety != saved_.variety) storage::saveSetting("dg2var", pr_.variety);
  if (pr_.scope != saved_.scope) storage::saveSetting("dg2scp", pr_.scope);
  if (pr_.song != saved_.song) storage::saveSetting("dg2song", pr_.song);
  if (pr_.len != saved_.len) storage::saveSetting("dg2len", kLens[pr_.len % kLenCount]);
  if (pr_.variant != saved_.variant) storage::saveSetting("dg2variant", pr_.variant);
  if (pr_.clean != saved_.clean) storage::saveSetting("dg2clean", pr_.clean);
  if (pr_.fixedSeed != saved_.fixedSeed) storage::saveSetting("dg2fixed", pr_.fixedSeed);
  if (pr_.keepKey != saved_.keepKey) storage::saveSetting("dg4key", pr_.keepKey);
  if (pr_.locks != saved_.locks) storage::saveSetting("dg3locks", pr_.locks);
  if (pr_.seed != saved_.seed) {
    storage::saveSetting("dg2seed0", static_cast<uint8_t>(pr_.seed));
    storage::saveSetting("dg2seed1", static_cast<uint8_t>(pr_.seed >> 8));
    storage::saveSetting("dg2seed2", static_cast<uint8_t>(pr_.seed >> 16));
  }
  if(pr_.target!=saved_.target)storage::saveSetting("dg3target",pr_.target);
  if(pr_.change!=saved_.change)storage::saveSetting("dg2change",pr_.change);
  if(pr_.form!=saved_.form)storage::saveSetting("dg2form",pr_.form);
  if(pr_.feel!=saved_.feel)storage::saveSetting("dg2feel",pr_.feel);
  if(pr_.density!=saved_.density)storage::saveSetting("dg2density",pr_.density);
  saved_ = pr_;
}

void DiceDialog::usedSeed(uint32_t seed,uint8_t variant) {
  pr_.seed = seed > 0 && seed <= kSeedMax ? seed : 1;
  last_.remember(pr_,variant);
  if (pr_ != saved_) app_.settingsChanged();
}

void DiceDialog::open() {
  clampPrefs();
  confirmed_ = {};  // a new session asks again
  list_.setEdit(false);
  list_.setSel(kGenre);
  actions_.reset(list_);
  open_ = true;
}

void DiceDialog::close() {
  if (!open_) return;
  open_ = false;
  list_.setEdit(false);
  if (pr_ != saved_) app_.settingsChanged();
}

void DiceDialog::roll() {
  list_.setEdit(false);
  mt::DiceOpts opts = options(pr_);
  opts.avoidVariant=last_.avoid(pr_);
  const int edit = app_.editPattern();
  const Range target = range(opts, edit);
  // Read-only, outside the lock: the UI is the only writer of the patterns.
  if ((opts.layers & mt::kDicePattern) && target != confirmed_ &&
      mt::diceOverwrites(app_.project(), edit, target.count) != 0) {
    char title[32];
    rangeTitle(title, sizeof(title), target, true);
    const MenuItem items[] = {{"Cancel", kMenuCancel}, {"Overwrite", kMenuOverwrite}};
    app_.menu().open(title, items, 2, [this, opts, target](int id) {
      if (id == kMenuOverwrite) rollNow(opts, target);
    });
    return;
  }
  rollNow(opts, target);
}

void DiceDialog::rollNow(const mt::DiceOpts& o, dice_dialog::Range target) {
  if (o.layers & mt::kDicePattern) confirmed_ = target;
  if (host_.roll) host_.roll(o);
  if (pr_ != saved_) app_.settingsChanged();
  app_.invalidate();
}

bool DiceDialog::action(int row) {
  if (row == kRoll) roll();
  else if (row == kClose) close();
  else return false;
  app_.invalidate();
  return true;
}

void DiceDialog::onInput(const hw::InputEvent& ev) {
  using hw::InputType;
  if (!open_) return;
  switch (ev.type) {
    case InputType::EncLong:
      close();
      app_.invalidate();
      return;
    case InputType::EncClick:
      if (!actions_.footerFocused() && !list_.editing() && action(list_.sel())) return;
      break;
    default: break;
  }
  const int result=actions_.input(list_,kRoll,ev);
  if(result>=0)action(result==0?kRoll:kClose);
}

void DiceDialog::onTouch(const TouchEvent& ev) {
  if (!open_) return;
  if(ev.y>=y0_+kAreaH-DialogActions::kH){
    // Touch ROLL without moving the encoder's current focus to the footer.
    if (ev.type == TouchType::Tap && ev.x >= kScreenW / 2) {
      action(kRoll);
      return;
    }
    const int result=actions_.touch(list_,ev,y0_);if(result>=0)action(result==0?kRoll:kClose);return;
  }
  actions_.touch(list_,ev,y0_);
}

void DiceDialog::draw(LGFX_Sprite& s, int y0) {
  if (!open_) return;
  y0_ = y0;
  char buf[32];
  s.fillRect(0, y0, kScreenW, kHeaderH - 4, kBeatBg);
  const mt::DiceOpts opts = options(pr_);
  const Range target = range(opts, app_.editPattern());
  if (patterns()) rangeTitle(buf, sizeof(buf), target, false);
  else snprintf(buf, sizeof(buf), "DICE");
  s.setTextColor(kText);
  s.drawString(buf, ParamList::kLabelX, y0 + (kHeaderH - 4 - kCharH) / 2);
  list_.draw(s, y0 + kHeaderH);
  actions_.draw(s,y0,"ROLL","CLOSE");
}

}  // namespace ui
