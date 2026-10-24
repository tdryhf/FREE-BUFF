// Host simulation: runs the real sketch against a simulated glass of water and two bolt pads.
// Build:  cp ../UnderpassFloodGuard.ino sketch2.cpp
//         g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Istubs -include Arduino.h -o sim2 harness2.cpp
// Run:    ./sim2          (exit code 0 = every check passed)
#include "Arduino.h"
#include "WiFi.h"
#include "WebServer.h"
#include "Preferences.h"
uint32_t g_millis = 0;
SerialStub Serial; WiFiStub WiFi; EspStub ESP;
std::map<std::string,uint32_t> g_nvs; std::map<std::string,std::vector<uint8_t>> g_nvsBlob; int g_nvsWrites = 0;
static std::string serialBuf; static std::vector<std::string> eventsSeen; static std::vector<std::string> serialLines;
void SerialStub::out(const char* c){ serialBuf += c; size_t p; while((p=serialBuf.find('\n'))!=std::string::npos){ std::string line=serialBuf.substr(0,p); serialBuf.erase(0,p+1); serialLines.push_back(line); if(line.rfind("# ",0)==0) eventsSeen.push_back(line);} }
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
// ---- checks ----
static int failures = 0;
static void expect(const char* what, bool ok, long got){ printf("   %s %-62s (got %ld)\n", ok?"PASS":"FAIL", what, got); if(!ok) failures++; }
static bool has(const std::string& s, const char* sub){ return s.find(sub)!=std::string::npos; }
static int countEvents(const char* sub){ int n=0; for(uint8_t i=0;i<evCount;i++){ const LogEntry& e=events[(evHead+EV_N-1-i)%EV_N]; if(strstr(e.msg,sub)) n++; } return n; }
// ---- runner ----
static std::string lastLine; static int falseWet=0; static bool prevWet[5];
static std::vector<int> levelsSeen; static bool trackLevels=false;
static void snapshot(){
  char leds[7]; for(int i=0;i<6;i++) leds[i]= pinState[LED_PIN[i]]?'#':'.'; leds[6]=0;
  const char* act[]={"heartbeat","slow","FAST","solid"};
  char line[300]; snprintf(line,sizeof line,"lvl=%d sev=%d %-8s LEDs=%.5s stop-light=%-9s | %-30s | btnA=%s",level,(int)sev,statusWord,leds,act[actionLed],headline,buttonALabel());
  if(lastLine!=line){ printf("t=%6.1fs water=%5.1fcm (=%3.0f cm road)  %s\n", g_millis/1000.0, waterH, waterH*6, line); lastLine=line; }
  for(auto&e:eventsSeen){ printf("            %s\n", e.c_str()); } eventsSeen.clear();
  for(int i=0;i<5;i++){ bool truth = waterH > tipH[i]; if(wet[i] && !prevWet[i] && !truth) falseWet++; prevWet[i]=wet[i]; }
  if(trackLevels && (levelsSeen.empty() || levelsSeen.back()!=level)) levelsSeen.push_back(level);
}
static void runUntil(uint32_t tEnd, std::function<void()> world){ while(g_millis < tEnd){ world(); loop(); snapshot(); g_millis += 25; } }
static std::string get(const char* path, const std::map<std::string,std::string>& a = {}){ return server.request(path, a); }
static void dumpJson(const char* name){ FILE* f=fopen(name,"w"); fputs(get("/data",{{"h","-1"},{"e","-1"}}).c_str(),f); fclose(f); }
static void tap(int k, uint32_t ms){ uint32_t t=g_millis+ms; runUntil(t, [k]{ padTouch[k]=true; }); padTouch[k]=false; }
static float ramp(float a, float b, uint32_t t0, uint32_t t1){ float f=((float)g_millis-(float)t0)/((float)t1-(float)t0); if(f<0)f=0; if(f>1)f=1; return a+(b-a)*f; }

// A power cut: everything in RAM is lost, the fake flash (g_nvs) stays,
// and the touch hardware starts cold again.
static void powerCut(){
  g_millis += 1000; chanInit.clear();
  level=0; lastLevel=-1; fault=false; road=ROAD_OPEN; counter=0; counterAt=0; counterAtKnown=false;
  closedAt=0; resumedClosure=false; testUntil=0; bannerUntil=0;
  for(int i=0;i<5;i++){ wet[i]=false; agree[i]=0; raw[i]=0; base[i]=0; }
  for(int i=0;i<=NUM_PROBES;i++) upAt[i]=0;
  riseFastUntil=0; chgT[0]=chgT[1]=0; chgL[0]=chgL[1]=-1;
  alarmOn=false; alarmAcked=false; alarmPlayNow=false; alarmLast=0; pat=nullptr;
  evCount=0; evHead=0; evSeq=0; logDirty=false; lastLogSave=0;
  histCount=0; histHead=0; histTotal=0;
  for(int k=0;k<2;k++){ padDown[k]=false; padLongDone[k]=false; padAgree[k]=0; }
  lastSample=lastPads=lastHist=lastPrint=0;
  sev=SEV_OK; statusWord="START"; headline="Starting..."; detail="";
}
static void bootAndWatch(uint32_t until){ levelsSeen.clear(); trackLevels=true; setup(); snapshot(); runUntil(until, []{}); trackLevels=false; }

int main(){
  printf("== boot, glass empty, nothing saved ==\n"); setup(); snapshot(); runUntil(5000, []{});
  expect("dry boot shows OPEN", !strcmp(statusWord,"OPEN"), level);
  dumpJson("t2_open.json");
  printf("== rain: water rises slowly ==\n");
  runUntil(15000, []{ waterH = ramp(0, 3.5f, 5000, 25000); });
  expect("5 cm shows CAUTION", !strcmp(statusWord,"CAUTION"), level);
  dumpJson("t2_caution.json");
  runUntil(25000, []{ waterH = ramp(0, 3.5f, 5000, 25000); });
  expect("closures after first storm == 1", counter==1, counter);
  expect("15 cm shows CLOSED with siren", !strcmp(statusWord,"CLOSED") && sirenActive(), level);
  expect("slow rise is NOT 'rising fast'", countEvents("Rising fast")==0, countEvents("Rising fast"));
  dumpJson("t2_closed.json");
  printf("== quick 0.2 s tap on bolt A to silence ==\n"); tap(0, 200);
  int b0 = buzzOnCount; runUntil(40000, []{});
  expect("no siren while silenced and water steady", buzzOnCount-b0==0, buzzOnCount-b0);
  printf("== water keeps rising to 10.5 cm (63 cm on the road) ==\n"); b0 = buzzOnCount;
  runUntil(70000, []{ waterH = ramp(3.5f, 10.5f, 40000, 70000); });
  expect("siren re-sounds on each rise after silencing", buzzOnCount-b0>0, buzzOnCount-b0);
  expect("emergency logged at 60 cm", countEvents("EMERGENCY")==1, countEvents("EMERGENCY"));
  dumpJson("t2_emergency.json");
  runUntil(80000, []{});
  printf("== rain stops: water drains ==\n");
  runUntil(110000, []{ waterH = ramp(10.5f, 1.5f, 80000, 105000); });
  expect("draining but above 5 cm stays CLOSED", road==ROAD_CLOSED, level);
  dumpJson("t2_falling.json");
  runUntil(125000, []{ waterH = ramp(1.5f, 0.4f, 110000, 120000); });
  expect("below 5 cm shows INSPECT", !strcmp(statusWord,"INSPECT"), level);
  dumpJson("t2_check.json");
  printf("== inspector taps bolt A ==\n"); tap(0, 300); runUntil(130000, []{});
  expect("tap A after inspection reopens", road==ROAD_OPEN, road);
  printf("== bolt B tap: siren test ==\n"); tap(1, 300); runUntil(140000, []{});
  printf("== second storm: fast rise ==\n");
  runUntil(150000, []{ waterH = ramp(0.4f, 6, 140000, 146000); });
  expect("closures after second storm == 2", counter==2, counter);
  expect("fast rise logs 'Rising fast' once", countEvents("Rising fast")==1, countEvents("Rising fast"));
  dumpJson("t2_fast.json");

  printf("== phone page: only new data is sent ==\n");
  std::string full = get("/data",{{"h","-1"},{"e","-1"}});
  expect("/data with h=-1 sends history and event log", has(full,"\"hist\":[0") && has(full,"\"ev\":["), (long)full.size());
  char hbuf[16], ebuf[16]; snprintf(hbuf,sizeof hbuf,"%lu",(unsigned long)histTotal); snprintf(ebuf,sizeof ebuf,"%lu",(unsigned long)evSeq);
  std::string delta = get("/data",{{"h",hbuf},{"e",ebuf}});
  expect("/data when up to date: no history points, no log", has(delta,"\"hist\":[]") && !has(delta,"\"ev\":"), (long)delta.size());
  expect("up-to-date answer is small (< 1000 bytes)", delta.size() < 1000, (long)delta.size());
  std::string sign = get("/data",{{"e","-2"}});
  expect("/sign data has no history or log", has(sign,"\"hist\":[]") && !has(sign,"\"ev\":"), (long)sign.size());
  std::string info = get("/info");
  expect("/info lists the depth thresholds", has(info,"\"lv\":[0,5,15,30,45,60]"), (long)info.size());
  std::string csv = get("/history.csv");
  expect("/history.csv has a header and rows", has(csv,"seconds_ago,level,depth") && std::count(csv.begin(),csv.end(),'\n')>50, (long)std::count(csv.begin(),csv.end(),'\n'));
  expect("/sign serves the sign page", has(get("/sign"),"Road Sign"), 0);
  { FILE* f=fopen("info2.json","w"); fputs(info.c_str(),f); fclose(f); }

  printf("== phone: silence, then drain and reopen from the phone ==\n");
  get("/cmd", {{"c","ok"}}); runUntil(152000, []{});
  runUntil(175000, []{ waterH = ramp(6, 0.3f, 152000, 168000); });
  get("/cmd", {{"c","ok"}}); runUntil(178000, []{});
  expect("phone button reopens after inspection", road==ROAD_OPEN, road);

  printf("== Serial commands ==\n");
  serialLines.clear(); Serial.in = "status\nHELP\nnope\n"; runUntil(178200, []{});
  bool st=false, hp=false, un=false; for(auto& l: serialLines){ if(has(l,"# Status:")) st=true; if(has(l,"# Commands:")) hp=true; if(has(l,"Unknown command: nope")) un=true; }
  expect("Serial 'status', 'help' and unknown command answered", st && hp && un, (long)serialLines.size());

  printf("== power cut during a flood: restart with 6 cm (36 cm road), touch hardware cold ==\n");
  int evBefore = evCount; waterH = 6; powerCut();
  b0 = buzzOnCount; bootAndWatch(186000);
  expect("restart in a flood closes the road", road==ROAD_CLOSED, road);
  expect("siren after restart in a flood", buzzOnCount-b0>0, buzzOnCount-b0);
  expect("closures survive the restart (2 saved + this one = 3)", counter==3, counter);
  expect("no transient false level at boot (levels seen: 3 only)", levelsSeen.size()==1 && levelsSeen[0]==3, (long)levelsSeen.size());
  expect("event log from before the restart was kept", evCount>=std::min<int>(EV_N, evBefore) && events[(evHead+EV_N-evCount)%EV_N].old==1, evCount);

  printf("== power cut while the road is CLOSED (same flood) ==\n");
  powerCut(); b0 = buzzOnCount; bootAndWatch(194000);
  expect("still CLOSED after restart", road==ROAD_CLOSED, road);
  expect("same flood is not counted twice (still 3)", counter==3, counter);
  expect("siren sounds again after restart", buzzOnCount-b0>0, buzzOnCount-b0);
  printf("== water drains, then a power cut while waiting for inspection ==\n");
  tap(0, 200);
  runUntil(215000, []{ waterH = ramp(6, 0.3f, 195000, 210000); });
  expect("INSPECT after draining", road==ROAD_CHECK, road);
  powerCut(); bootAndWatch(220000);
  expect("restart does NOT reopen the road by itself", road==ROAD_CHECK && !strcmp(statusWord,"INSPECT"), road);
  tap(0, 300); runUntil(223000, []{});
  expect("tap A after inspection reopens", road==ROAD_OPEN, road);

  printf("== dry restart with the road open ==\n");
  waterH = 0; powerCut(); bootAndWatch(230000);
  expect("dry restart = OPEN, no siren", road==ROAD_OPEN && !sirenActive() && !strcmp(statusWord,"OPEN"), road);

  printf("== flash wear ==\n");
  expect("flash writes for the whole run stay low (< 400)", g_nvsWrites < 400, g_nvsWrites);

  FILE* f=fopen("page2.html","w"); fputs(get("/").c_str(),f); fclose(f);
  f=fopen("sign2.html","w"); fputs(get("/sign").c_str(),f); fclose(f);
  expect("false WET onsets (neighbour-wire sag down to ~58%)", falseWet==0, falseWet);
  printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", failures);
  return failures ? 1 : 0;
}
