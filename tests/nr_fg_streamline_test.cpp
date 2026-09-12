#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "../src/nr_source_frame.hpp"
// CPU contract test of the actual adapter. SDK calls and MinHook outcomes are
// explicit doubles; this does not claim Streamline/NVIDIA execution.
enum MH_STATUS{MH_OK,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_ENABLED,MH_ERROR_MEMORY_ALLOC};
unsigned creates=0,enables=0;bool enable_failure=true;
MH_STATUS MH_Initialize(){return MH_OK;}
MH_STATUS MH_CreateHook(void* target,void*,void** original){++creates;*original=target;return MH_OK;}
MH_STATUS MH_EnableHook(void*){++enables;return enable_failure?MH_ERROR_MEMORY_ALLOC:MH_OK;}
namespace nr_processing_runtime{std::atomic<bool> work_enabled{true};}
namespace adapter {
using nr_source_frame::Id;using nr_source_frame::Source;
SRWLOCK source_lock=SRWLOCK_INIT;
struct Lock{Lock(){AcquireSRWLockExclusive(&source_lock);}~Lock(){ReleaseSRWLockExclusive(&source_lock);}};
thread_local Source sl_source{},sl_present_source{};
Source PublishStreamlineSource(Id provider,Id epoch,unsigned view,unsigned frame){return {provider^((Id(view)+1)<<32),epoch,Id(frame)+1};}
struct StreamlineScope{Source previous;explicit StreamlineScope(Source value):previous(sl_source){sl_source=value;}~StreamlineScope(){sl_source=previous;}};
#include "../src/nr_fg_streamline.inl"
}
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
int constants_result=0;adapter::Source seen{};
int FakeConstants(const void*,const void*,const void*){return constants_result;}
int FakeTags(const void*,const void*,const void*,unsigned,void*){return constants_result;}
int FakeEvaluate(unsigned,const void*,const void*const*,unsigned,void*){seen=adapter::sl_source;return 0;}
int FakeMarker(unsigned,const void*){return 0;}
unsigned FrameSequence(const void* pointer){return *reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(pointer)+sizeof(adapter::sl_wire::Frame));}
struct Token{adapter::sl_wire::Frame header{};unsigned value=0;};
int main(){try{using namespace adapter;using namespace adapter::sl_wire;
    void* vtable[]={reinterpret_cast<void*>(&FrameSequence)};Token token{};token.header.vtable=vtable;
    token.header.base.type=frame_type;token.header.base.version=1;token.value=0;
    Viewport viewport{};viewport.base.type=viewport_type;viewport.base.version=1;viewport.value=7;
    unsigned value=99;Check(FrameValue(&token,value)&&value==0,"public frame zero is a real source token");
    token.header.base.version=2;Check(!FrameValue(&token,value),"unknown frame ABI version rejected");token.header.base.version=1;
    viewport.base.version=2;Check(!ViewValue(&viewport,value),"unknown viewport ABI rejected");viewport.base.version=1;
    Check(ViewValue(&viewport,value)&&value==7,"public viewport identity read");
    provider=GetModuleHandleW(nullptr);constants=FakeConstants;tags=FakeTags;evaluate=FakeEvaluate;marker=FakeMarker;enabled=true;
    Check(HookConstants(nullptr,&token,&viewport)==0,"original constants result preserved");auto first=OneView(&token);
    Check(bool(first)&&first.sequence==1,"frame zero published with nonzero internal source");
    HookTags(&token,&viewport,nullptr,0,nullptr);Check(OneView(&token)==first,"constants and tag observation share one identity");
    const void* inputs[]={&viewport};HookEvaluate(1000,&token,inputs,1,nullptr);
    Check(seen==first&&!sl_source,"unknown feature brand can use common frame scope and scope restores");
    HookMarker(4,&token);Check(sl_present_source==first,"actual application PresentStart binds exact frame");
    nr_processing_runtime::work_enabled=false;HookMarker(5,&token);Check(!sl_present_source,"PresentEnd expires old source while F6 is off");nr_processing_runtime::work_enabled=true;
    HookMarker(4,&token);HookMarker(11,&token);Check(!sl_present_source,"out of band Present never lends source identity");
    Viewport second=viewport;second.value=8;HookConstants(nullptr,&token,&second);
    Check(!OneView(&token),"ambiguous multiple view frame cannot guess source");
    HookEvaluate(1000,&token,inputs,1,nullptr);Check(seen==first,"explicit viewport resolves multiple view source");
    token.value=1;constants_result=-1;HookConstants(nullptr,&token,&viewport);Check(!OneView(&token),"failed SDK constants call cannot publish new frame");constants_result=0;
    HookConstants(nullptr,&token,&viewport);Check(OneView(&token).sequence==2,"next successful real frame advances source");
    void* original=nullptr;
    Check(!Install(reinterpret_cast<void*>(&FakeMarker),reinterpret_cast<void*>(&HookMarker),&original)&&original&&!Ready(&original),"create success with enable failure stays uninstalled");
    Check(creates==1&&enables==1,"one owned disabled hook retained");enable_failure=false;
    Check(Install(reinterpret_cast<void*>(&FakeMarker),reinterpret_cast<void*>(&HookMarker),&original)&&Ready(&original),"owned disabled hook can retry enable");
    Check(creates==1&&enables==2,"retry does not create duplicate hook or adopt foreign trampoline");
    next_poll=GetTickCount64()+10000;const auto before=creates;Poll();Check(creates==before,"poll honors backoff");
    std::printf("Streamline source adapter CPU: %u checks passed; SDK/FG execution not tested\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
