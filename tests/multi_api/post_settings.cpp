// Compile and exercise actual setting implementation using a host API seam.
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include "src/interop/nr_post_compat.h"
using ULONGLONG=unsigned long long;
std::atomic<unsigned> g_config_generation{1},g_history_generation{1};
namespace nr_f8 { std::atomic<bool> g_config_hold{false}; }
struct ChainConfig {int mode=2;bool enabled=true;} g_config;
bool ApplyConfig(ChainConfig next,const char*) {
 if(nr_f8::g_config_hold.load() || next.enabled==g_config.enabled)return false;
 g_config=next;++g_config_generation;++g_history_generation;return true;
}
wchar_t g_ini_path[]=L"test.ini";
int g_chain_cs=0,locks=0,writes=0;
bool persist=true,hold_on_lock=false,revision_on_lock=false;
unsigned rr_ini=1,pct_ini=100;
ULONGLONG tick=1000;
void EnterCriticalSection(int*);
void LeaveCriticalSection(int*){--locks;}
ULONGLONG GetTickCount64(){return tick;}
void Log(const char*,...){}
unsigned GetPrivateProfileIntW(const wchar_t*,const wchar_t* key,unsigned,const wchar_t*) {
 return std::wcscmp(key,L"RRPostEnabled")==0?rr_ini:pct_ini;
}
bool WriteIniInt(const wchar_t* key,int value){
 ++writes;if(persist){if(std::wcscmp(key,L"RRPostEnabled")==0)rr_ini=value;else pct_ini=value;}return persist;
}
#include "src/interop/nr_post_settings.inl"
void EnterCriticalSection(int*){
 ++locks;
 if(hold_on_lock)nr_f8::g_config_hold=true;
 if(revision_on_lock)nr_post_settings::revision.fetch_add(1);
}
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 using namespace nr_post_settings;
 auto initial=Read();CHECK(!initial.rr&&initial.percent==100);
 Poll();CHECK(Read().rr&&ready);
 Request({true,75});CHECK(Read().percent==75&&g_config.mode==2&&writes==2&&locks==0);
 auto gen=g_config_generation.load();auto hist=g_history_generation.load();
 auto request_revision=revision.load();
 Request({true,75});CHECK(g_config_generation==gen&&g_history_generation==hist);
 CHECK(revision.load()>request_revision); // invalidate a stale concurrent read even on no-op

 for(unsigned invalid:{1u,49u,101u,1000u,0xffffffffu}) {
  CHECK(!Request({false,invalid}));
  CHECK(Read().rr&&Read().percent==75&&pct_ini==75&&rr_ini==1&&request_rejected);
 }
 CHECK(Request({false,50}));CHECK(!Read().rr&&Read().percent==50&&pct_ini==50&&rr_ini==0);
 nr_f8::g_config_hold=true;auto previous=bits.load();auto previous_writes=writes;
 Request({true,100});CHECK(bits==previous&&writes==previous_writes);
 nr_f8::g_config_hold=false;hold_on_lock=true;
 Request({true,100});CHECK(bits==previous&&writes==previous_writes&&locks==0);
 hold_on_lock=false;nr_f8::g_config_hold=false;
 persist=false;Request({true,67});CHECK(Read().percent==67&&persistence_failed);
 tick+=1000;Poll();CHECK(Read().percent==67);
 persist=true;Request({true,100});CHECK(!persistence_failed&&Read().percent==100);
 pct_ini=75;rr_ini=0;tick+=1000;Poll();CHECK(!Read().rr&&Read().percent==75&&locks==0);
 gen=g_config_generation.load();Poll();CHECK(g_config_generation==gen);
 tick+=1000;pct_ini=67;revision_on_lock=true;Poll();CHECK(Read().percent==75);
 revision_on_lock=false;tick+=1000;Poll();CHECK(Read().percent==67);
 CHECK(g_config.mode==2&&locks==0);
 CHECK(Request({true,0}));CHECK(!g_config.enabled&&Read().percent==67&&pct_ini==67);
 CHECK(Request({true,75}));CHECK(!g_config.enabled&&Read().percent==75);
 g_config.enabled=true;pct_ini=0;tick+=1000;Poll();
 CHECK(!g_config.enabled&&Read().percent==75&&pct_ini==75);
 g_config.enabled=true;tick+=1000;Poll();CHECK(g_config.enabled);
 pct_ini=49;tick+=1000;Poll();CHECK(g_config.enabled&&Read().percent==75&&request_rejected);
 nr_f8::g_config_hold=true;CHECK(!Request({true,0}));CHECK(g_config.enabled);
 nr_f8::g_config_hold=false;
 std::printf("%u actual setting/hold/persistence/revision checks passed (CPU seam)\n",checks);
}
