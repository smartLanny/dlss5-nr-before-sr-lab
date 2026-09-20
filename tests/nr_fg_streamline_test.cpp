#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "../src/nr_source_frame.hpp"
#include "../src/nr_fg_presentation.h"
#include "../src/nr_rr_projection_source.h"
// CPU contract test of the actual adapter. SDK calls and MinHook outcomes are
// explicit doubles; this does not claim Streamline/NVIDIA execution.
enum MH_STATUS{MH_OK,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_ENABLED,MH_ERROR_MEMORY_ALLOC};
unsigned creates=0,enables=0;bool enable_failure=true;
MH_STATUS MH_Initialize(){return MH_OK;}
MH_STATUS MH_CreateHook(void* target,void*,void** original){++creates;*original=target;return MH_OK;}
MH_STATUS MH_EnableHook(void*){++enables;return enable_failure?MH_ERROR_MEMORY_ALLOC:MH_OK;}
namespace nr_processing_runtime{std::atomic<bool> work_enabled{true};}
nr_fg_presentation::Policy g_fg_presentation_policy;
std::atomic<unsigned> g_history_generation{1};
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
nr_rr_depth::CurrentInput seen_projection;
int projection_params;
int FakeEvaluate(unsigned,const void*,const void*const*,unsigned,void* commands){
 seen=adapter::sl_source;
 nr_rr_projection::NativeScope native(commands,&projection_params,17);
 seen_projection=nr_rr_projection::Choose({},&projection_params,17);return 0;
}
int FakeMarker(unsigned,const void*){return 0;}
int fg_options_result=0;bool advance_options_lifecycle=false;
const void* seen_options_view=nullptr;const void* seen_options=nullptr;
int FakeFgOptions(const void* viewport,const void* options){
    seen_options_view=viewport;seen_options=options;
    if(advance_options_lifecycle){g_fg_presentation_policy.BeginCreate();g_fg_presentation_policy.EndCreate();}
    return fg_options_result;
}
int FakeGetFunction(unsigned feature,const char* name,void*& function){
    if(feature==1000&&name&&std::strcmp(name,"slDLSSGSetOptions")==0){function=reinterpret_cast<void*>(&FakeFgOptions);return 0;}
    return -9;
}
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
    provider=GetModuleHandleW(nullptr);constants=FakeConstants;tags=FakeTags;evaluate=FakeEvaluate;
    get_function=FakeGetFunction;marker=FakeMarker;enabled=true;
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
    ConstantsPrefix cp{};cp.base.type=constants_type;cp.base.version=2;
    cp.camera_view_to_clip[0]=cp.camera_view_to_clip[5]=cp.camera_view_to_clip[11]=1;
    cp.camera_view_to_clip[14]=.1f;
    HookConstants(&cp,&token,&viewport);
    HookEvaluate(1001,&token,inputs,1,reinterpret_cast<void*>(123));
    Check(seen_projection.state==nr_rr_depth::InputState::Supported,"same-frame RR scope supplies copied projection");
    cp.camera_view_to_clip[14]=9.f; // caller storage is no longer consulted
    HookEvaluate(1001,&token,inputs,1,reinterpret_cast<void*>(123));
    Check(seen_projection.projection.b==.1f,"camera copy survives caller mutation");
    HookEvaluate(1000,&token,inputs,1,reinterpret_cast<void*>(123));
    Check(seen_projection.state==nr_rr_depth::InputState::Absent,"FG cannot borrow RR projection");
    constants_result=-1;HookConstants(&cp,&token,&viewport);constants_result=0;
    HookEvaluate(1001,&token,inputs,1,reinterpret_cast<void*>(123));
    Check(seen_projection.state==nr_rr_depth::InputState::Absent,"failed same-frame constants invalidate cached camera");
    Check(!nr_rr_projection::evaluation.source,"projection scope ends with SDK Evaluate");
    void* original=nullptr;
    Check(!Install(reinterpret_cast<void*>(&FakeMarker),reinterpret_cast<void*>(&HookMarker),&original)&&original&&!Ready(&original),"create success with enable failure stays uninstalled");
    Check(creates==1&&enables==1,"one owned disabled hook retained");enable_failure=false;
    Check(Install(reinterpret_cast<void*>(&FakeMarker),reinterpret_cast<void*>(&HookMarker),&original)&&Ready(&original),"owned disabled hook can retry enable");
    Check(creates==1&&enables==2,"retry does not create duplicate hook or adopt foreign trampoline");
    void* options_function=nullptr;
    Check(HookGetFunction(1000,"slDLSSGSetOptions",options_function)==0&&
        options_function==reinterpret_cast<void*>(&HookFgOptions),"real feature factory result is wrapped");
    FgOptionsPrefix options{};options.base.type=fg_options_type;options.base.version=5;options.mode=1;
    g_fg_presentation_policy.Inventory(1,1);
    auto set_options=reinterpret_cast<FgOptionsFn>(options_function);
    Check(set_options(&viewport,&options)==0&&seen_options_view==&viewport&&seen_options==&options&&
        g_fg_presentation_policy.Read().reason==nr_fg_presentation::Reason::OptionsEnabled,
        "successful On passes exact arguments and records public ABI");
    options.mode=0;Check(set_options(&viewport,&options)==0&&!g_fg_presentation_policy.Read().late_owned,
        "successful Off overrides the current live handle");
    GUID unknown=options.base.type;unknown.Data1^=1;options.base.type=unknown;options.mode=1;
    Check(set_options(&viewport,&options)==0&&!g_fg_presentation_policy.Read().late_owned,
        "unknown options GUID cannot change ownership");
    options.base.type=fg_options_type;options.base.version=6;
    Check(set_options(&viewport,&options)==0&&!g_fg_presentation_policy.Read().late_owned,
        "future options version is not parsed as known");
    options.base.version=5;fg_options_result=-17;
    Check(set_options(&viewport,&options)==-17&&!g_fg_presentation_policy.Read().late_owned,
        "failed original On result is returned and ignored");
    fg_options_result=0;Check(set_options(&viewport,&options)==0&&g_fg_presentation_policy.Read().late_owned,
        "successful On is observed after the original returns");
    options.mode=0;fg_options_result=-23;
    Check(set_options(&viewport,&options)==-23&&g_fg_presentation_policy.Read().late_owned,
        "failed original Off result cannot authorize final Present");
    fg_options_result=0;Check(set_options(&viewport,&options)==0&&!g_fg_presentation_policy.Read().late_owned,
        "successful Off restores the current lifetime before rollover test");
    options.mode=1;advance_options_lifecycle=true;
    Check(set_options(&viewport,&options)==0&&g_fg_presentation_policy.Read().options==0&&
        g_fg_presentation_policy.Read().reason==nr_fg_presentation::Reason::LiveHandleModeUnknown,
        "successful call crossing Feature11 lifecycle cannot publish stale options");
    advance_options_lifecycle=false;
    next_poll=GetTickCount64()+10000;const auto before=creates;Poll();Check(creates==before,"poll honors backoff");
    std::printf("Streamline source adapter CPU: %u checks passed; SDK/FG execution not tested\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
