#pragma once
#define sdReady fileUiOriginalSdReady
#include "../../storage_stubs/hw/sdcard.h"
#undef sdReady
namespace hw {
inline bool ready = true;
inline bool sdReady(){return ready;}
inline bool sdRecover(){return ready;}
inline bool sdBegin(){return ready;}
inline int sdList(const char* dir,const char* ext,char (*names)[kNameMax],int cap,bool(*accept)(const char*)){int n=0;for(const auto& e:fs::files){const std::string prefix=std::string(dir)+"/";if(e.first.find(prefix)!=0)continue;auto name=e.first.substr(prefix.size());if(name.find('/')!=std::string::npos||name.size()<strlen(ext)||name.substr(name.size()-strlen(ext))!=ext)continue;name.resize(name.size()-strlen(ext));if(accept&&!accept(name.c_str()))continue;if(n<cap)strlcpy(names[n++],name.c_str(),kNameMax);}return n;}
inline int sdListDirs(const char*,char (*)[kNameMax],int){return 0;}
}
