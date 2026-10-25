#pragma once
#include "Arduino.h"
// Fake flash: survives a simulated reboot because it lives in the harness.
extern std::map<std::string, uint32_t> g_nvs;
extern std::map<std::string, std::vector<uint8_t>> g_nvsBlob;
extern int g_nvsWrites;
struct Preferences {
  std::string ns;
  std::string k(const char* key){ return ns + "/" + key; }
  bool begin(const char* name, bool=false){ ns = name; return true; } void end(){}
  bool getBool(const char* key, bool d){ auto it=g_nvs.find(k(key)); return it==g_nvs.end()?d:it->second; }
  size_t putBool(const char* key, bool v){ g_nvs[k(key)]=v; g_nvsWrites++; return 1; }
  uint32_t getUInt(const char* key, uint32_t d){ auto it=g_nvs.find(k(key)); return it==g_nvs.end()?d:it->second; }
  size_t putUInt(const char* key, uint32_t v){ g_nvs[k(key)]=v; g_nvsWrites++; return 4; }
  size_t putBytes(const char* key, const void* v, size_t n){ auto p=(const uint8_t*)v; g_nvsBlob[k(key)].assign(p,p+n); g_nvsWrites++; return n; }
  size_t getBytesLength(const char* key){ auto it=g_nvsBlob.find(k(key)); return it==g_nvsBlob.end()?0:it->second.size(); }
  size_t getBytes(const char* key, void* buf, size_t n){ auto it=g_nvsBlob.find(k(key)); if(it==g_nvsBlob.end()) return 0; size_t m=std::min(n,it->second.size()); memcpy(buf,it->second.data(),m); return m; }
};
