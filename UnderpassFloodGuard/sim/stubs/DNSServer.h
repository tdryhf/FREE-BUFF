#pragma once
#include "WiFi.h"
struct DNSServer { bool start(const uint16_t&, const String&, const IPAddress&){ return true; } void processNextRequest(){} };
