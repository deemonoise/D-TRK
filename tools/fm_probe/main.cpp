#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include "synth_fm_machines.h"
#include "synth_osc.h"
namespace {
mt::FmVoice* voices;
mt::FmParams* params;
float pcm[128];
volatile float checksum;
constexpr uint8_t defaults[10][8]={
 {64,0,0,80,48,32,64,0},{64,0,0,80,32,64,64,0},{64,0,0,80,32,64,64,0},{64,0,0,80,32,64,64,0},
 {64,64,0,80,64,48,64,0},{64,64,0,80,64,64,64,0},{64,32,0,110,16,64,0,0},{64,32,0,80,64,64,64,0},
 {64,64,0,110,0,64,0,0},{64,64,32,80,0,0,127,0}};
constexpr const char* names[] = {"BD_MODERN","SD_BASIC","SD_VINTAGE","PC_CARBON","CP_VINTAGE","CY_ALLOY","CHORD","TOY","TONE","HH_BASIC"};
void bench(int machine,int count,int stress) {
 float raw[8];for(int k=0;k<8;++k)raw[k]=defaults[machine][k];
 raw[3]=127; // keep the one-shots alive throughout the ladder
 if(stress) {raw[7]=127;raw[2]=127;raw[1]=127;raw[4]=127;}
 if(machine==6 && stress>=3){raw[1]=96;raw[4]=16;raw[5]=127;raw[6]=127;}
 const bool moving=stress==2||stress==5||stress==6;
 const int base=(stress==3||stress==5||stress==7)?48:stress?100:48;
 for(int v=0;v<count;++v){voices[v]=mt::FmVoice();mt::fmMachine(machine,raw,base+v%3*4,params[v]);voices[v].trigger(false);}
 int operators=0;for(const auto& op:params[0].op)operators+=op.level!=0;
 if(machine==6&&stress>=3&&operators!=8){Serial.println("FATAL CHORD stress did not activate eight operators");return;}
 Serial.printf("CONFIG machine=%s stress=%d pitch=%d moving=%d operators=%d over=%.3f retrigger_blocks=8 timed_trigger=1\n",names[machine],stress,base,moving,operators,params[0].sat);
 uint64_t total=0;uint32_t peak=0;int active=0;float energy=0,signalPeak=0;
 constexpr int warm=32,blocks=256;
 for(int b=0;b<warm+blocks;++b){
  const uint64_t start=esp_timer_get_time();
  if(b%8==0)for(int v=0;v<count;++v)voices[v].trigger(false);
  for(float& x:pcm)x=0;
  for(int sub=0;sub<4;++sub)for(int v=0;v<count;++v){
   if(moving){raw[0]=64+32*sinf((b*4+sub)*.03f+v);mt::fmMachine(machine,raw,base+v%3*4,params[v]);}
   voices[v].control(params[v],32);voices[v].render(pcm+sub*32,32,1.f);
  }
  const uint32_t us=esp_timer_get_time()-start;
  checksum=checksum+pcm[b%128]*1e-8f;
  if(b>=warm)for(float sample:pcm){
   if(!isfinite(sample)){Serial.println("FATAL non-finite PCM");return;}
   energy+=sample*sample;signalPeak=fmaxf(signalPeak,fabsf(sample));
  }
  if(b>=warm){total+=us;if(us>peak)peak=us;for(int v=0;v<count;++v)active+=!voices[v].done();}
  if((b&15)==0)delay(1);
 }
 Serial.printf("NEWFM %s voices=%d stress=%d active=%.2f avg_us=%.2f peak_us=%lu load=%.3f checksum=%.9g rms=%.6g pcm_peak=%.6g\n",names[machine],count,stress,active/float(blocks),total/float(blocks),static_cast<unsigned long>(peak),total/float(blocks*4000),checksum,sqrtf(energy/(blocks*128)),signalPeak);
}
}
void setup(){
 rgbLedWrite(RGB_BUILTIN,0,0,0);
 Serial.begin(115200);delay(3000);
 Serial.printf("NEWFM hardware cpu=%u flash=%lu psram=%lu rate=%d source=4000b2e\n",ESP.getCpuFreqMHz(),ESP.getFlashChipSize(),ESP.getPsramSize(),mt::kSynthRate);
 if(ESP.getFlashChipSize()!=16*1024*1024||ESP.getPsramSize()!=8*1024*1024){Serial.println("FATAL expected N16R8");return;}
 void* voiceMemory=heap_caps_malloc(sizeof(mt::FmVoice)*8,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 void* parameterMemory=heap_caps_malloc(sizeof(mt::FmParams)*8,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!voiceMemory||!parameterMemory){Serial.println("FATAL insufficient internal RAM");return;}
 voices=new(voiceMemory)mt::FmVoice[8];
 params=new(parameterMemory)mt::FmParams[8];
 Serial.printf("NEWFM sizes voice=%u params=%u free_internal=%lu\n",sizeof(mt::FmVoice),sizeof(mt::FmParams),heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
 for(int m=0;m<10;++m)for(int stress=0;stress<3;++stress)for(int n:{1,2,4,8})bench(m,n,stress);
 // Eight-operator CHORD: four notes plus FENV upper partials, even-rich WAVE96.
 for(int stress:{3,4,5,6})for(int n:{1,2,4,8})bench(6,n,stress);
 // TONE PNCH at lower pitch retains more harmonics than the high-pitch stress.
 for(int n:{1,2,4,8})bench(8,n,7);
 Serial.println("NEWFM DONE");
}
void loop(){delay(1000);}
