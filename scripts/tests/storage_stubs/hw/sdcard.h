#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "project_io.h"
#define FILE_READ "r"
#define FILE_WRITE "w"
namespace fs {
using Bytes=std::vector<uint8_t>;
inline std::map<std::string,Bytes> files;
inline std::set<std::string> dirs;
inline std::set<std::string> failures;
inline std::vector<std::string> operations;
inline std::string corruptOnClose;
inline size_t corruptOffset=0;
inline bool fail(const std::string& op) {operations.push_back(op);return failures.count(op);}
struct File {
 std::string path; size_t pos=0; bool opened=false;
 explicit operator bool() const {return opened;}
 bool isDirectory() const {return dirs.count(path);}
 size_t size() const {return files.at(path).size();}
 int read(uint8_t* p,size_t n) {auto& b=files.at(path);n=std::min(n,b.size()-pos);memcpy(p,b.data()+pos,n);pos+=n;return static_cast<int>(n);}
 size_t write(const uint8_t* p,size_t n) {if(fail("write:"+path))return 0;auto& b=files[path]; if(b.size()<pos+n)b.resize(pos+n);memcpy(b.data()+pos,p,n);pos+=n;return n;}
 size_t position() const {return pos;}
 bool seek(size_t p) {if(p>size())return false;pos=p;return true;}
 void close() {if(opened&&path==corruptOnClose&&corruptOffset<files[path].size())files[path][corruptOffset]^=1;opened=false;}
};
struct FS {
 bool exists(const char* p) {return files.count(p)||dirs.count(p);}
 File open(const char* p,const char* mode=FILE_READ) {if(dirs.count(p))return {p,0,true};if(fail("open:"+std::string(p)))return {};if(mode[0]=='w')files[p].clear();if(!files.count(p))return {};return {p,0,true};}
 bool remove(const char* p) {return !fail("remove:"+std::string(p))&&files.erase(p);}
 bool rename(const char* a,const char* b) {if(fail("rename:"+std::string(a)+":"+b)||!files.count(a)||exists(b))return false;files[b]=files[a];files.erase(a);return true;}
 bool mkdir(const char* p) {dirs.insert(p);return true;}
};
}
namespace hw {
inline bool sdReady() {return true;}
inline fs::FS& sdFs() {static fs::FS f;return f;}
class FileSink : public mt::ByteSink {
 fs::File& f;
 public: explicit FileSink(fs::File& v):f(v){}
 bool write(const void* p,size_t n) override {return f.write(static_cast<const uint8_t*>(p),n)==n;}
 bool flush() {return true;}
};
class FileSource : public mt::ByteSource {
 fs::File& f;
 public: explicit FileSource(fs::File& v):f(v){}
 bool read(void* p,size_t n) override {return f.read(static_cast<uint8_t*>(p),n)==static_cast<int>(n);}
 bool skip(size_t n) override {return f.seek(f.position()+n);}
};
constexpr int kNameMax=96;
inline int sdListFiles(const char* dir,const char* const* exts,int count,char (*out)[kNameMax],int max) {
 int n=0;const std::string prefix=std::string(dir)+"/";
 for(const auto& kv:fs::files){if(kv.first.find(prefix)!=0)continue;const auto name=kv.first.substr(prefix.size());if(name.find('/')!=std::string::npos)continue;
 for(int i=0;i<count&&n<max;++i)if(name.size()>=strlen(exts[i])&&name.substr(name.size()-strlen(exts[i]))==exts[i]){strncpy(out[n++],name.c_str(),kNameMax);break;}}
 return n;
}
}
