// Compile and exercise actual setting implementation using a host API seam.
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include "src/interop/nr_post_compat.h"
using ULONGLONG=unsigned long long;
std::atomic<unsigned> g_config_generation{1},g_history_generation{1};
std::atomic<unsigned long long> g_all_evaluations{900},g_rr_guarded_sr_evaluations{12};
namespace nr_f8 { std::atomic<bool> g_config_hold{false}; }
struct ChainConfig {int mode=2;bool enabled=true,color_protection=true;} g_config;
bool ApplyConfig(ChainConfig next,const char*) {
 if(nr_f8::g_config_hold.load() || next.enabled==g_config.enabled)return false;
 g_config=next;++g_config_generation;++g_history_generation;return true;
}
wchar_t g_ini_path[]=L"test.ini";
int g_chain_cs=0,locks=0,writes=0;
bool persist=true,hold_on_lock=false,revision_on_lock=false;
unsigned rr_ini=1,pct_ini=100;
unsigned missing_ini=0;
ULONGLONG tick=1000;
void EnterCriticalSection(int*);
void LeaveCriticalSection(int*){--locks;}
ULONGLONG GetTickCount64(){return tick;}
void Log(const char*,...){}
unsigned GetPrivateProfileIntW(const wchar_t*,const wchar_t* key,unsigned,const wchar_t*) {
 if(std::wcscmp(key,L"RRMissingProjectionFallback")==0)return missing_ini;
 return std::wcscmp(key,L"RRPostEnabled")==0?rr_ini:pct_ini;
}
bool WriteIniInt(const wchar_t* key,int value){
 ++writes;if(persist){if(std::wcscmp(key,L"RRMissingProjectionFallback")==0)missing_ini=value;
 else if(std::wcscmp(key,L"RRPostEnabled")==0)rr_ini=value;else pct_ini=value;}return persist;
}
#include "src/interop/nr_post_settings.inl"
void EnterCriticalSection(int*){
 ++locks;
 if(hold_on_lock)nr_f8::g_config_hold=true;
 if(revision_on_lock)nr_post_settings::revision.fetch_add(1);
}
struct FeedbackSession { std::string text; };
void FeedbackAppend(FeedbackSession& session,const char* format,...) {
 char text[4096];va_list args;va_start(args,format);
 const int n=std::vsnprintf(text,sizeof(text),format,args);va_end(args);
 if(n<0 || static_cast<unsigned>(n)>=sizeof(text))std::abort();
 session.text.append(text,static_cast<unsigned>(n));
}
enum class RrDepthReject : unsigned { None };
const char* RrDepthRejectName(RrDepthReject){return "none";}
bool IsCompatibilityPostObserved(){return false;}
int EffectivePictureMode(int requested){return requested;}
std::atomic<RrDepthReject> g_rr_depth_last_reject{RrDepthReject::None};
std::atomic<unsigned long long> g_rr_depth_proxy_matches{0};
std::atomic<int> g_compat_fallback_reason{0};
std::atomic<unsigned long long> g_compat_fallback_attempts{0},g_compat_fallback_recorded{0};
std::atomic<unsigned> g_compat_fallback_cause{0};
std::atomic<unsigned long long> g_native_command_records{0},g_native_command_retirements{0};
std::atomic<unsigned long long> g_native_command_record_failures{0};
std::atomic<unsigned long long> g_rr_depth_conversions{0},g_rr_depth_declines{0};
void EmitPostFeedback(FeedbackSession& s) {
 const char* prefix="capture";
#include "src/interop/nr_post_feedback.inl"
}
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 using namespace nr_post_settings;
 auto initial=Read();CHECK(initial.rr&&initial.percent==100&&RrPostAutomatic());
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
 CHECK(Request({false,50}));CHECK(Read().rr&&Read().percent==50&&pct_ini==50&&rr_ini==1);
 nr_f8::g_config_hold=true;auto previous=bits.load();auto previous_writes=writes;
 Request({true,100});CHECK(bits==previous&&writes==previous_writes);
 nr_f8::g_config_hold=false;hold_on_lock=true;
 Request({true,100});CHECK(bits==previous&&writes==previous_writes&&locks==0);
 hold_on_lock=false;nr_f8::g_config_hold=false;
 persist=false;Request({true,67});CHECK(Read().percent==67&&persistence_failed);
 tick+=1000;Poll();CHECK(Read().percent==67);
 persist=true;Request({true,100});CHECK(!persistence_failed&&Read().percent==100);
 pct_ini=75;rr_ini=0;tick+=1000;Poll();CHECK(Read().rr&&Read().percent==75&&locks==0);
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
 // Capture changes are temporary, require the existing hold, and restore the
 // exact work percentage without enabling NR, changing Mode or writing INI.
 auto saved=Read();auto capture_writes=writes;auto mode=g_config.mode;
 CHECK(!RequestForCapture({false,50}));
 nr_f8::g_config_hold=true;g_config.enabled=false;
 CHECK(RequestForCapture({false,50}));CHECK(Read().rr&&Read().percent==50);
 CHECK(!RequestForCapture({true,0}));CHECK(!RequestForCapture({true,49}));
 CHECK(!RequestForCapture({true,101}));CHECK(Read().percent==50);
 CHECK(RequestForCapture(saved));CHECK(Read().percent==saved.percent);
 CHECK(writes==capture_writes&&g_config.mode==mode&&!g_config.enabled&&locks==0);
 nr_f8::g_config_hold=false;
 rr_attempts=2;rr_recorded=1;rr_native_failures=0;last_rr_tick=tick;last_rr_stage=2;
 FeedbackSession current;EmitPostFeedback(current);
 CHECK(current.text.find("capture.post.request.percent=75\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.requestRejected=1\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.rejectedPercent=49\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.rr.nrRecordings=1\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.evaluateEntries=900\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.rr.srGuarded=12\n")!=std::string::npos);
 CHECK(current.text.find("capture.post.rr.recentAttempt=1\n")!=std::string::npos);
 CHECK(current.text.find("not GPU completion")!=std::string::npos);
 tick+=3001;FeedbackSession historical;EmitPostFeedback(historical);
 CHECK(historical.text.find("capture.post.rr.recentAttempt=0\n")!=std::string::npos);
 last_rr_tick=0;FeedbackSession unavailable;EmitPostFeedback(unavailable);
 CHECK(unavailable.text.find("capture.post.rr.attemptAgeMs=-1\n")!=std::string::npos);
 CHECK(!missing_projection);
 nr_f8::g_config_hold=true;CHECK(!RequestMissingProjection(true));CHECK(!missing_projection);
 nr_f8::g_config_hold=false;
 gen=g_config_generation;hist=g_history_generation;
 CHECK(RequestMissingProjection(true));CHECK(missing_projection&&missing_ini==1);
 CHECK(g_config_generation==gen+1&&g_history_generation==hist+1);
 Request({true,100});CHECK(missing_projection&&missing_ini==1); // Scale is not a permission editor.
 missing_ini=0;tick+=1000;Poll();CHECK(!missing_projection);
 nr_rr_depth::observations.input=nr_rr_depth::InputState::Absent;
 nr_rr_depth::observations.placeholder_recorded=3;
 FeedbackSession fallback;EmitPostFeedback(fallback);
 CHECK(fallback.text.find("depthAdapter.currentInput=missing-current-matrix\n")!=std::string::npos);
 CHECK(fallback.text.find("missingProjection.recorded=3\n")!=std::string::npos);
 CHECK(fallback.text.find("depthAdapter.recorded=0\n")!=std::string::npos);
 std::printf("%u actual setting/hold/persistence/revision checks passed (CPU seam)\n",checks);
}
