// Host simulation: runs the real sketch against a simulated glass of water and two bolt pads.
#include "Arduino.h"
#include "WiFi.h"
#include "WebServer.h"
#include "Preferences.h"
uint32_t g_millis = 0;
SerialStub Serial; WiFiStub WiFi; std::map<std::string,uint32_t> g_nvs;
static std::string serialBuf; static std::vector<std::string> eventsSeen;
void SerialStub::out(const char* c){ serialBuf += c; size_t p; while((p=serialBuf.find('\n'))!=std::string::npos){ std::string line=serialBuf.substr(0,p); serialBuf.erase(0,p+1); if(line.rfind("# ",0)==0) eventsSeen.push_back(line);} }
static uint8_t pinState[40]; static int buzzOnCount=0;
void delay(uint32_t ms){ g_millis += ms; }
void pinMode(uint8_t, uint8_t){}
void digitalWrite(uint8_t pin, uint8_t v){ if(pin==23 && v && !pinState[23]) buzzOnCount++; pinState[pin]=v; }
void tone(uint8_t, unsigned int, unsigned long){} void noTone(uint8_t){}
// ---- simulated world ----
float waterH = 0;                          // cm of water above the glass bottom
const float tipH[5] = {0.8f, 2.5f, 5, 7.5f, 10};  // theme-2 dipstick tips (cm above the point)
bool padTouch[2] = {false,false};
bool splashP4 = false;
std::map<int,uint32_t> chanInit;           // a channel reads 0 for 30 ms after its first use
static uint32_t rng=12345; static int noise(int a){ rng=rng*1103515245u+12345u; return (int)((rng>>16)%(2*a+1))-a; }
touch_value_t touchRead(uint8_t pin){
  if(!chanInit.count(pin)) chanInit[pin]=g_millis;
  if(g_millis - chanInit[pin] < 30) return 0;
  const uint8_t probes[5]={32,33,27,14,13};
  for(int i=0;i<5;i++) if(pin==probes[i]){
    bool w = waterH > tipH[i] || (i==3 && splashP4);
    if(w) return 3+noise(2);                              // wet: oscillation almost stalls
    int wetBelow=0; for(int j=0;j<i;j++) if(waterH > tipH[j]) wetBelow++;
    float sag = 1.0f - 0.09f*wetBelow;                    // coupling to wet neighbour wires
    if(waterH > tipH[i]-0.6f) sag -= 0.06f;               // water surface just below the tip
    return (touch_value_t)(62*sag) + noise(3);
  }
  if(pin==4)  return padTouch[0] ? 14+noise(3) : 74+noise(3);
  if(pin==15) return padTouch[1] ? 15+noise(3) : 71+noise(3);
  return 0;
}
#include "sketch2.cpp"
// ---- runner ----
static std::string lastLine; static int falseWet=0; static bool prevWet[5];
static void snapshot(){
  char leds[7]; for(int i=0;i<6;i++) leds[i]= pinState[LED_PIN[i]]?'#':'.'; leds[6]=0;
  const char* act[]={"heartbeat","slow","FAST","solid"};
  char line[300]; snprintf(line,sizeof line,"lvl=%d sev=%d LEDs=%.5s stop-light=%-9s | %-30s | btnA=%s",level,(int)sev,leds,act[actionLed],headline,buttonALabel());
  if(lastLine!=line){ printf("t=%6.1fs water=%5.1fcm (=%3.0f cm road)  %s\n", g_millis/1000.0, waterH, waterH*6, line); lastLine=line; }
  for(auto&e:eventsSeen) printf("            %s\n", e.c_str()); eventsSeen.clear();
  for(int i=0;i<5;i++){ bool truth = waterH > tipH[i]; if(wet[i] && !prevWet[i] && !truth) falseWet++; prevWet[i]=wet[i]; }
}
static void runUntil(uint32_t tEnd, std::function<void()> world){ while(g_millis < tEnd){ world(); loop(); snapshot(); g_millis += 25; } }
static void dumpJson(const char* name){ FILE* f=fopen(name,"w"); fputs(server.request("/data").c_str(),f); fclose(f); }
static void tap(int k, uint32_t ms){ uint32_t t=g_millis+ms; runUntil(t, [k]{ padTouch[k]=true; }); padTouch[k]=false; }
static float ramp(float a, float b, uint32_t t0, uint32_t t1){ float f=(float)(g_millis-t0)/(t1-t0); if(f<0)f=0; if(f>1)f=1; return a+(b-a)*f; }
int main(){
  printf("== boot, glass empty, nothing saved ==\n"); setup(); snapshot(); runUntil(5000, []{});
  printf("== rain: water rises slowly ==\n");
  runUntil(25000, []{ waterH = ramp(0, 3.5f, 5000, 25000); });
  printf("   closures so far (expect 1): %d\n", counter);
  printf("== quick 0.2 s tap on bolt A to silence ==\n"); tap(0, 200);
  int b0 = buzzOnCount; runUntil(40000, []{}); printf("   siren starts while silenced, water steady (expect 0): %d\n", buzzOnCount-b0);
  printf("== water keeps rising to 10.5 cm (63 cm on the road) ==\n"); b0 = buzzOnCount;
  runUntil(70000, []{ waterH = ramp(3.5f, 10.5f, 40000, 70000); });
  printf("   siren re-sounded on escalation after being silenced (expect > 0): %d\n", buzzOnCount-b0);
  dumpJson("t2_emergency.json");
  runUntil(80000, []{});
  printf("== rain stops: water drains ==\n");
  runUntil(110000, []{ waterH = ramp(10.5f, 1.5f, 80000, 105000); });
  dumpJson("t2_falling.json");
  runUntil(125000, []{ waterH = ramp(1.5f, 0.4f, 110000, 120000); });
  dumpJson("t2_check.json");
  printf("== inspector taps bolt A ==\n"); tap(0, 300); runUntil(130000, []{});
  printf("== bolt B tap: siren test ==\n"); tap(1, 300); runUntil(140000, []{});
  printf("== second storm: fast rise ==\n");
  runUntil(150000, []{ waterH = ramp(0.4f, 6, 140000, 146000); });
  dumpJson("t2_closed.json");
  printf("   closures so far (expect 2): %d\n", counter);
  printf("== phone: silence, then drain and reopen from the phone ==\n");
  server.request("/cmd", {{"c","ok"}}); runUntil(152000, []{});
  runUntil(175000, []{ waterH = ramp(6, 0.3f, 152000, 168000); });
  server.request("/cmd", {{"c","ok"}}); runUntil(178000, []{});
  printf("== power cut during a flood: restart with 6 cm (36 cm road), touch hardware cold ==\n");
  waterH = 6; g_millis += 1000; chanInit.clear(); level=0; lastLevel=-1; road=ROAD_OPEN; counter=0;
  for(int i=0;i<5;i++){wet[i]=false;agree[i]=0;} alarmOn=false; alarmAcked=false; evCount=0; evHead=0;
  b0 = buzzOnCount; setup(); runUntil(186000, []{});
  printf("   siren after restart in a flood (expect > 0): %d   closures (expect 1): %d\n", buzzOnCount-b0, counter);
  FILE* f=fopen("page2.html","w"); fputs(server.request("/").c_str(),f); fclose(f);
  printf("CHECK false WET onsets (expect 0): %d\n", falseWet);
  return 0;
}
