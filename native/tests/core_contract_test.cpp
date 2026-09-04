// Contract tests against a small fake core. This is NOT a production core build.
#include <atomic>
#include <cmath>
#include <cassert>
#include <cstdint>
#define __declspec(x)
int g_chain_cs=0;bool busy=false;int calls=0;
int TryEnterCriticalSection(int*){return !busy;}
void LeaveCriticalSection(int*){}
struct ChainConfig {bool enabled=true,auto_mask=false;int mode=2,work_mode=1;unsigned style=0;float transfer_strength=1.2f,post_transfer_strength=1.2f,custom_work_scale=.75f;};
ChainConfig g_config;
std::atomic<unsigned>g_config_generation{1};std::atomic<int>g_last_bypass_reason{0};std::atomic<unsigned long long>g_nr_transfer_submitted_frames{0};std::atomic<bool>g_config_persistence_failed{false};
namespace nr_f8 {std::atomic<bool>g_config_hold{false};}
bool ApplyConfig(ChainConfig value,const char*,bool){g_config=value;++g_config_generation;++calls;return true;}
#include "../../core-integration/nrui_exports.inl"
int main(){NrUiStateV1 s{};s.size=sizeof(s);s.version=1;assert(NRUI_GetStateV1(&s)==NRUI_OK);assert(s.generation==1);assert(NRUI_SetValueV1(NRUI_STRENGTH,2,1)==NRUI_OK);assert(g_config.transfer_strength==2&&g_config.post_transfer_strength==2);assert(NRUI_SetValueV1(NRUI_ENABLED,0,1)==NRUI_CONFLICT);nr_f8::g_config_hold=true;assert(NRUI_SetValueV1(NRUI_ENABLED,0,2)==NRUI_BUSY);nr_f8::g_config_hold=false;assert(NRUI_SetValueV1(NRUI_STRENGTH,NAN,2)==NRUI_INVALID);assert(calls==1);busy=true;assert(NRUI_GetStateV1(&s)==NRUI_BUSY);}
