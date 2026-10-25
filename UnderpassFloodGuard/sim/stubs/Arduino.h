// Minimal host-side stand-in for the Arduino-ESP32 API (signatures follow the real core)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <string>
#include <functional>
#include <map>
#include <vector>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
typedef uint16_t touch_value_t;
extern uint32_t g_millis;
inline uint32_t millis(){ return g_millis; }
void delay(uint32_t ms);
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t pin, uint8_t val);
touch_value_t touchRead(uint8_t pin);
void tone(uint8_t pin, unsigned int f, unsigned long d=0);
void noTone(uint8_t pin);
class String {
 public:
  std::string s;
  String() {}
  String(const char* c): s(c?c:"") {}
  String(const std::string& x): s(x) {}
  bool reserve(unsigned int n){ s.reserve(n); return true; }
  const char* c_str() const { return s.c_str(); }
  unsigned int length() const { return s.size(); }
  String& operator+=(const char* c){ s += c; return *this; }
  String& operator+=(const String& o){ s += o.s; return *this; }
  String& operator+=(char c){ s += c; return *this; }
  String& operator+=(unsigned char n){ s += std::to_string((unsigned)n); return *this; }
  String& operator+=(int n){ s += std::to_string(n); return *this; }
  String& operator+=(unsigned int n){ s += std::to_string(n); return *this; }
  String& operator+=(long n){ s += std::to_string(n); return *this; }
  String& operator+=(unsigned long n){ s += std::to_string(n); return *this; }
  bool operator==(const char* c) const { return s == c; }
  long toInt() const { return s.empty() ? 0 : strtol(s.c_str(), nullptr, 10); }
};
struct SerialStub {
  void begin(unsigned long){}
  template<typename T> void print(T v){ out(v); }
  template<typename T> void println(T v){ out(v); out("\n"); }
  void println(){ out("\n"); }
  void out(const char* c);
  void out(const String& v){ out(v.c_str()); }
  void out(long v){ out(std::to_string(v).c_str()); }
  void out(unsigned long v){ out(std::to_string(v).c_str()); }
  void out(int v){ out(std::to_string(v).c_str()); }
  void out(unsigned int v){ out(std::to_string(v).c_str()); }
  void out(char* c){ out((const char*)c); }
  // typed input for the Serial-command tests: harness pushes text into `in`
  std::string in;
  int available(){ return (int)in.size(); }
  int read(){ if(in.empty()) return -1; int c=(unsigned char)in[0]; in.erase(0,1); return c; }
};
extern SerialStub Serial;
struct EspStub { uint32_t getFreeHeap(){ return 215000; } };
extern EspStub ESP;
