#include <Arduino.h>
#include <cassert>
#include <memory>
#include "hw/sdcard.h"
#include "engine/engine.h"
#include "storage/storage.h"
#include "storage/presets.h"
#include "link/bank_client.h"
#include "preset_io.h"
#include "storage/file_commit.h"

namespace audio {
BankResult assetResult=BankResult::Ok;
BankResult saveAssets(const char*,BankSets*,BankItemFn,void*) {return assetResult;}
BankResult syncProject(const char*,BankSets*,BankItemFn,void*) {return BankResult::Ok;}
const char* bankResultText(BankResult) {return "test";}
}
using storage::Result;

void reset() {fs::files.clear();fs::dirs.clear();fs::failures.clear();fs::operations.clear();fs::corruptOnClose.clear();audio::assetResult=audio::BankResult::Ok;engine::queueFull=false;}
std::unique_ptr<mt::Project> project() {auto p=std::make_unique<mt::Project>();p->reset();strlcpy(p->name,"song",sizeof(p->name));return p;}

void preset_rename_failure_keeps_old() {
 reset();mt::Instrument m{};
 assert(storage::savePreset("/presets/CHIP","sound",m)==Result::Ok);
 const auto old=fs::files.at("/presets/CHIP/sound.mti");
 fs::failures.insert("rename:/presets/CHIP/sound.tmp:/presets/CHIP/sound.mti");
 m.vol=63;
 assert(storage::savePreset("/presets/CHIP","sound",m)==Result::WriteFail);
 assert(fs::files.count("/presets/CHIP/sound.mti"));
 assert(fs::files.at("/presets/CHIP/sound.mti")==old);
}
void resource_failure_does_not_publish() {
 reset();auto p=project();assert(storage::save(*p,"song")==Result::Ok);
 assert(storage::autosave(*p)==Result::Ok);
 const auto old=fs::files.at("/projects/song.mtp"),autoSave=fs::files.at("/projects/song.auto");
 audio::assetResult=audio::BankResult::WriteFail;
 p->bpm=145;p->sampleCount=1;strlcpy(p->samples[0].name,"kick",sizeof(p->samples[0].name));
 assert(storage::save(*p,"song")==Result::SamplesNotSaved);
 assert(fs::files.at("/projects/song.mtp")==old);
 assert(fs::files.at("/projects/song.auto")==autoSave);
 assert(storage::save(*p,"copy")==Result::SamplesNotSaved);
 assert(std::string(p->name)=="song");
 assert(!fs::files.count("/projects/copy.mtp"));
}
void autosave_and_template_failures() {
 reset();auto p=project();assert(storage::autosave(*p)==Result::Ok);
 auto old=fs::files.at("/projects/song.auto");p->bpm=145;
 fs::failures.insert("rename:/projects/song.atm:/projects/song.auto");
 assert(storage::autosave(*p)==Result::WriteFail);
 assert(fs::files.count("/projects/song.auto"));assert(fs::files.at("/projects/song.auto")==old);
 reset();assert(storage::saveTemplate(*p,"base")==Result::Ok);old=fs::files.at("/templates/base.mtp");
 fs::failures.insert("rename:/templates/base.tmp:/templates/base.mtp");p->bpm=160;
 assert(storage::saveTemplate(*p,"base")==Result::WriteFail);
 assert(fs::files.count("/templates/base.mtp"));assert(fs::files.at("/templates/base.mtp")==old);
}
void full_queue_replace() {
 reset();auto p=project();assert(storage::save(*p,"song")==Result::Ok);
 p->bpm=180;engine::queueFull=true;
 assert(storage::load(*p,"song")==Result::Ok);
 assert(engine::tempo==p->bpm);assert(engine::resets==1);assert(engine::locks==0);
}
void interrupted_rotations_recover() {
 reset();auto p=project();assert(storage::autosave(*p)==Result::Ok);
 assert(hw::sdFs().rename("/projects/song.auto","/projects/song.aub"));
 assert(storage::autosaveExists("song"));
 p->bpm=190;assert(storage::loadAutosave(*p)==Result::Ok);assert(p->bpm==120);
 mt::Instrument m{};m.vol=73;assert(storage::savePreset("/presets/CHIP","sound",m)==Result::Ok);
 assert(hw::sdFs().rename("/presets/CHIP/sound.mti","/presets/CHIP/sound.bak"));
 assert(storage::presetExists("/presets/CHIP","sound"));
 m.vol=5;assert(storage::loadPreset("/presets/CHIP","sound",m)==Result::Ok);assert(m.vol==73);
 assert(storage::removePreset("/presets/CHIP","sound")==Result::Ok);
 assert(!storage::presetExists("/presets/CHIP","sound"));
 assert(storage::saveTemplate(*p,"base")==Result::Ok);
 assert(hw::sdFs().rename("/templates/base.mtp","/templates/base.bak"));
 assert(storage::newFromTemplate(*p,"base")==Result::Ok);

}
void rotation_failure_matrix() {
 const char* failures[]={"remove:backup","rename:dst:backup","rename:tmp:dst"};
 for(const char* failure:failures) {
  reset();fs::files["tmp"]={3};fs::files["dst"]={2};fs::files["backup"]={1};
  fs::failures.insert(failure);
  assert(!storage::commitFile(hw::sdFs(),"tmp","dst","backup"));
  assert(fs::files.at("dst")==fs::Bytes{2});assert(fs::files.at("tmp")==fs::Bytes{3});
 }
 reset();fs::files["tmp"]={3};fs::files["dst"]={2};
 fs::failures={"rename:tmp:dst","rename:backup:dst"};
 assert(!storage::commitFile(hw::sdFs(),"tmp","dst","backup"));
 assert(fs::files.at("backup")==fs::Bytes{2});assert(fs::files.at("tmp")==fs::Bytes{3});
 assert(!storage::commitFile(hw::sdFs(),"tmp","dst","backup"));
 assert(fs::files.at("backup")==fs::Bytes{2});
 fs::failures.clear();assert(storage::recoverFile(hw::sdFs(),"dst","backup"));
 assert(fs::files.at("dst")==fs::Bytes{2});
 assert(storage::commitFile(hw::sdFs(),"tmp","dst","backup"));
 assert(fs::files.at("dst")==fs::Bytes{3});assert(fs::files.at("backup")==fs::Bytes{2});
 assert(!storage::commitFile(hw::sdFs(),"missing","dst","backup"));
}
void resource_cleanup_after_commit() {
 reset();auto p=project();p->sampleCount=1;strlcpy(p->samples[0].name,"kick",sizeof(p->samples[0].name));
 fs::dirs.insert("/projects/song");fs::files["/projects/song/kick.wav"]={1};
 assert(storage::save(*p,"song")==Result::Ok);
 p->sampleCount=0;
 assert(storage::save(*p,"song")==Result::Ok);
 assert(fs::files.count("/projects/song/kick.wav")); // still referenced by the new backup
 assert(storage::save(*p,"song")==Result::Ok);
 assert(!fs::files.count("/projects/song/kick.wav")); // now neither version needs it
}
void project_commit_failure_preserves_autosave() {
 reset();auto p=project();assert(storage::save(*p,"song")==Result::Ok);
 assert(storage::autosave(*p)==Result::Ok);const auto old=fs::files.at("/projects/song.mtp");
 fs::failures.insert("rename:/projects/song.tmp:/projects/song.mtp");p->bpm=160;
 assert(storage::save(*p,"song")==Result::WriteFail);
 assert(fs::files.at("/projects/song.mtp")==old);assert(storage::autosaveExists("song"));
}
void corrupt_autosave_is_not_published() {
 reset();auto p=project();assert(storage::autosave(*p)==Result::Ok);const auto old=fs::files.at("/projects/song.auto");
 fs::corruptOnClose="/projects/song.atm";p->bpm=150;
 assert(storage::autosave(*p)==Result::WriteFail);assert(fs::files.at("/projects/song.auto")==old);
}
void unreadable_backup_defers_cleanup() {
 reset();auto p=project();fs::dirs.insert("/projects/song");
 fs::files["/projects/song/orphan.wav"]={1};
 assert(storage::save(*p,"song")==Result::Ok);
 fs::files["/projects/song/orphan.wav"]={1};
 fs::failures.insert("open:/projects/song.bak");
 assert(storage::save(*p,"song")==Result::Ok);
 assert(fs::files.count("/projects/song/orphan.wav"));
}
int main(int argc,char** argv) {
 assert(argc==2);const std::string c=argv[1];
 if(c=="preset")preset_rename_failure_keeps_old();
 else if(c=="resources")resource_failure_does_not_publish();
 else if(c=="other_files")autosave_and_template_failures();
 else if(c=="queue")full_queue_replace();
 else if(c=="recovery")interrupted_rotations_recover();
 else if(c=="matrix")rotation_failure_matrix();
 else if(c=="cleanup")resource_cleanup_after_commit();
 else if(c=="corrupt")corrupt_autosave_is_not_published();
 else if(c=="backup_read")unreadable_backup_defers_cleanup();
 else if(c=="project_failure")project_commit_failure_preserves_autosave();
 else assert(false);
}
