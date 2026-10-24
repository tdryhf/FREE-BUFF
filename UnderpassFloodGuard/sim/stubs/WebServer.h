#pragma once
#include "Arduino.h"
class WebServer {
 public:
  WebServer(int){}
  void on(const char* p, std::function<void()> f){ routes[p]=f; }
  void onNotFound(std::function<void()> f){ nf=f; }
  void begin(){}
  void handleClient(){}
  void send(int code, const char* type, const String& body){ lastCode=code; lastType=type; lastBody=body.s; }
  String arg(const char* n){ return String(args[n]); }
  // test hook
  std::string request(const std::string& path, const std::map<std::string,std::string>& a = {}){
    args=a; auto it=routes.find(path); if(it!=routes.end()) it->second(); else nf(); return lastBody; }
  std::map<std::string,std::function<void()>> routes; std::function<void()> nf;
  std::map<std::string,std::string> args; int lastCode=0; std::string lastType, lastBody;
};
