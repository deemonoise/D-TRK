#include <Arduino.h>
#include <cassert>
#include <memory>
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "engine/engine.h"
#include "storage/storage.h"
#include "storage/presets.h"
#include "audio/bank.h"
#include "preset_io.h"
#include "sample_set.h"


namespace audio {
BankResult assetResult=BankResult::Ok;
int importCalls=0;
mt::SampleBank& bank() {static TestFlash flash;static mt::SampleBank b(flash);return b;}
bool bankMounted() {return true;}
BankResult exportWav(int,const char*) {return assetResult;}
BankResult exportWt(const char*,const mt::Project&,const char*) {return assetResult;}
BankResult migrateProject(mt::Project&,int*) {return BankResult::Ok;}
bool legacySource(uint32_t,uint32_t,char[17],char[17]) {return false;}
BankResult importToCache(const char*,const mt::Project&,ImportOut&,const uint32_t*,BankProgressFn,void*) {++importCalls;return BankResult::Ok;}
BankResult importWtToCache(const char*,const mt::Project&,uint32_t&,const uint32_t*,BankProgressFn,void*) {return BankResult::Ok;}
const char* bankResultText(BankResult) {return "test";}
}
using storage::Result;

void reset() {fs::files.clear();fs::dirs.clear();fs::failures.clear();fs::operations.clear();fs::corruptOnClose.clear();audio::assetResult=audio::BankResult::Ok;engine::queueFull=false;engine::savedTempo=0;engine::countIn=0;engine::stops=0;}
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
 p->bpm=145;p->sampleCount=1;strlcpy(p->samples[0].name,"kick",sizeof(p->samples[0].name));
 p->samples[0].crc=123;p->samples[0].frames=64;
 audio::bank().mount();char key[17];mt::sampleKey(123,key);assert(audio::bank().begin(key,64,32000,60));int16_t data[64]={};assert(audio::bank().write(data,64));assert(audio::bank().commit());
 audio::assetResult=audio::BankResult::WriteFail;
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
void snapshot_preserves_extension_fields_and_saved_tempo() {
 reset();auto p=project();p->bpm=155;engine::savedTempo=123;
 p->compAttack=27;p->songLen=2;p->songTrn[1]=-7;p->songBpm[1]=201;p->songTrnMask=0xA50F;
 strlcpy(p->patterns[2].name,"CHORUS",sizeof(p->patterns[2].name));
 assert(storage::save(*p,"song")==Result::Ok);engine::savedTempo=0;
 p->compAttack=83;p->songTrn[1]=0;p->songBpm[1]=0;p->songTrnMask=0;
 p->patterns[2].name[0]=0;
 assert(storage::load(*p,"song")==Result::Ok);
 assert(p->bpm==123&&p->compAttack==27&&p->songTrn[1]==-7&&p->songBpm[1]==201);
 assert(p->songTrnMask==0xA50F&&!strcmp(p->patterns[2].name,"CHORUS"));
}
void template_strip_keeps_sound_and_removes_song_extensions() {
 reset();auto p=project();p->compAttack=27;p->songLen=1;p->song[0]=3;
 p->songTrn[0]=7;p->songBpm[0]=160;p->songTrnMask=0x1234;
 strlcpy(p->patterns[3].name,"DROP",sizeof(p->patterns[3].name));
 p->patterns[3].steps[0][0].note=60;
 assert(storage::saveTemplate(*p,"base")==Result::Ok);
 assert(storage::newFromTemplate(*p,"base")==Result::Ok);
 assert(p->compAttack==27&&p->songTrnMask==0x1234&&p->songLen==0);
 assert(p->songTrn[0]==0&&p->songBpm[0]==0&&p->patterns[3].name[0]==0);
 assert(!p->patterns[3].steps[0][0].hasNote());
}
void loading_during_count_in_stops_before_replacing_project() {
 reset();auto p=project();assert(storage::save(*p,"song")==Result::Ok);
 engine::countIn=4;p->bpm=180;
 assert(storage::load(*p,"song")==Result::Ok);
 assert(engine::stops==1&&engine::countIn==0&&p->bpm==120);
 engine::queueFull=true;engine::countIn=4;p->bpm=180;
 assert(storage::load(*p,"song")==Result::EngineBusy);
 assert(engine::countIn==4&&p->bpm==180);
}
void staging_budget_failure_preserves_the_live_project_and_external_state() {
 reset();
 auto p=project();
 p->sampleCount=1;
 strlcpy(p->samples[0].name,"kick",sizeof(p->samples[0].name));
 p->samples[0].crc=123;p->samples[0].frames=64;
 assert(storage::save(*p,"song")==Result::Ok);
 fs::files["/projects/song/kick.wav"]={1,2,3};
 p->bpm=188;
 strlcpy(p->patterns[4].name,"UNSAVED",sizeof(p->patterns[4].name));
 p->patterns[4].steps[2][3].note=71;
 const auto liveBytes=std::vector<uint8_t>(reinterpret_cast<uint8_t*>(p.get()),reinterpret_cast<uint8_t*>(p.get())+sizeof(*p));
 const auto card=fs::files;
 const int oldResets=engine::resets;
 engine::countIn=4;engine::tempo=188;audio::importCalls=0;fs::operations.clear();
 assert(host_heap::psramUsed==0);
 // Fifty KiB of a browser/work buffer leaves the staging model one byte short.
 constexpr size_t heldBytes=51200;
 host_heap::psramBudget=sizeof(mt::Project)+heldBytes-1;
 void* held=heap_caps_malloc(heldBytes,MALLOC_CAP_SPIRAM);assert(held);
 const size_t rejectedBefore=host_heap::rejected;
 assert(storage::load(*p,"song")==Result::NoMemory);
 assert(host_heap::rejected==rejectedBefore+1);
 assert(!memcmp(p.get(),liveBytes.data(),sizeof(*p)));
 assert(engine::countIn==4&&engine::stops==0&&engine::resets==oldResets&&engine::tempo==188);
 assert(audio::importCalls==0);
 assert(fs::files==card); // includes /last.txt and project resources
 assert(fs::operations.empty()); // even rewriting the same /last.txt must not happen
 assert(host_heap::psramUsed==heldBytes);
 // Restored budget permits the same transactional load, with the staging allocation freed.
 heap_caps_free(held);
 assert(storage::load(*p,"song")==Result::Ok);
 assert(p->bpm==120&&p->patterns[4].name[0]==0);
 assert(engine::countIn==0&&engine::stops==1&&engine::resets==oldResets+1);
 assert(audio::importCalls==1);
 assert(host_heap::psramUsed==0&&host_heap::psramBlocks.empty());
 host_heap::psramBudget=std::numeric_limits<size_t>::max();
}

int main(int argc,char** argv) {
 assert(argc==2);const std::string c=argv[1];
 if(c=="heap_budget")staging_budget_failure_preserves_the_live_project_and_external_state();
 else if(c=="preset")preset_rename_failure_keeps_old();
 else if(c=="resources")resource_failure_does_not_publish();
 else if(c=="other_files")autosave_and_template_failures();
 else if(c=="queue")full_queue_replace();
 else if(c=="recovery")interrupted_rotations_recover();
 else if(c=="cleanup")resource_cleanup_after_commit();
 else if(c=="corrupt")corrupt_autosave_is_not_published();
 else if(c=="backup_read")unreadable_backup_defers_cleanup();
 else if(c=="project_failure")project_commit_failure_preserves_autosave();
 else if(c=="count_in")loading_during_count_in_stops_before_replacing_project();
 else if(c=="snapshot_fields")snapshot_preserves_extension_fields_and_saved_tempo();
 else if(c=="template_fields")template_strip_keeps_sound_and_removes_song_extensions();
 else assert(false);
}
