#pragma once
#include "Arduino.h"
extern std::map<std::string, uint32_t> g_nvs;
struct Preferences {
  bool begin(const char*, bool=false){ return true; } void end(){}
  bool getBool(const char* k, bool d){ auto it=g_nvs.find(std::string("B")+k); return it==g_nvs.end()?d:it->second; }
  size_t putBool(const char* k, bool v){ g_nvs[std::string("B")+k]=v; return 1; }
  uint32_t getUInt(const char* k, uint32_t d){ auto it=g_nvs.find(std::string("U")+k); return it==g_nvs.end()?d:it->second; }
  size_t putUInt(const char* k, uint32_t v){ g_nvs[std::string("U")+k]=v; return 4; }
};
