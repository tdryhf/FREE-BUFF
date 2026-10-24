#pragma once
#include "Arduino.h"
#define WIFI_AP 2
struct IPAddress { uint8_t a[4]; operator const char*() const { return "192.168.4.1"; } };
struct WiFiStub { void mode(int){} bool softAP(const char* ssid, const char* pw){ return ssid && pw && strlen(pw)>=8; } IPAddress softAPIP(){ return IPAddress{{192,168,4,1}}; } };
extern WiFiStub WiFi;
