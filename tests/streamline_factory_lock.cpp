// The complete production adapter is included below. These Win32/MinHook
// doubles model lock order and module pin contracts, not Windows execution.
#include <atomic>
#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "../src/nr_source_frame.hpp"
#include "../src/nr_fg_presentation.h"
#include "../src/nr_rr_projection_source.h"

using DWORD=std::uint32_t;
using ULONGLONG=std::uint64_t;
using SIZE_T=std::size_t;
using HMODULE=void*;
using LPCWSTR=const wchar_t*;
struct GUID {std::uint32_t Data1;std::uint16_t Data2,Data3;unsigned char Data4[8];};
bool IsEqualGUID(const GUID& a,const GUID& b){return std::memcmp(&a,&b,sizeof(GUID))==0;}
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_PIN=1,GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=4;
std::atomic<unsigned> next_thread{1};
unsigned Thread(){static thread_local const unsigned value=next_thread.fetch_add(1);return value;}
struct SRWLOCK {std::timed_mutex mutex;std::atomic<unsigned> owner{0};};
#define SRWLOCK_INIT {}
SRWLOCK* observed_install=nullptr;
std::atomic<unsigned> blocking_install_attempts{0};
void AcquireSRWLockExclusive(SRWLOCK* lock){
    if(lock==observed_install)++blocking_install_attempts;
    if(lock->owner.load()==Thread())throw std::runtime_error("recursive install-lock wait");
    // A bounded diagnostic replaces a process that would otherwise hang. The
    // owner cannot release until the tested factory callback has returned.
    if(!lock->mutex.try_lock_for(std::chrono::milliseconds(250)))
        throw std::runtime_error("factory/loader install-lock cycle");
    lock->owner.store(Thread());
}
bool TryAcquireSRWLockExclusive(SRWLOCK* lock){
    if(lock->owner.load()==Thread()||!lock->mutex.try_lock())return false;
    lock->owner.store(Thread());return true;
}
void ReleaseSRWLockExclusive(SRWLOCK* lock){lock->owner.store(0);lock->mutex.unlock();}
ULONGLONG GetTickCount64(){return 10000;}
void* GetCurrentProcess(){return nullptr;}
bool ReadProcessMemory(void*,const void* source,void* destination,SIZE_T bytes,SIZE_T* copied){
    if(!source)return false;
    std::memcpy(destination,source,bytes);*copied=bytes;return true;
}

std::recursive_mutex loader_lock;
std::mutex rendezvous_lock;
std::condition_variable rendezvous;
bool signal_poll=false,poll_waiting_loader=false;
std::vector<const void*> pinned_addresses;
std::vector<const void*> unloaded_addresses;
const void* fail_pin=nullptr;
const void* racing_target_a=nullptr;
const void* racing_target_b=nullptr;
unsigned racing_expected=0,racing_arrived=0;
HMODULE provider_module=reinterpret_cast<HMODULE>(std::uintptr_t(0x10000));
HMODULE module_for_name=provider_module;
unsigned named_lookups=0;
bool GetModuleHandleExW(DWORD flags,LPCWSTR name,HMODULE* result){
    if(!(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)&&signal_poll){
        std::lock_guard lock(rendezvous_lock);poll_waiting_loader=true;rendezvous.notify_all();
    }
    if(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS){
        const auto address=reinterpret_cast<const void*>(name);
        {
            std::lock_guard lock(loader_lock);
            if(address==fail_pin||std::find(unloaded_addresses.begin(),unloaded_addresses.end(),address)!=unloaded_addresses.end())return false;
            if(flags&GET_MODULE_HANDLE_EX_FLAG_PIN)pinned_addresses.push_back(address);
            *result=const_cast<void*>(address);
        }
        if(racing_expected&&(address==racing_target_a||address==racing_target_b)){
            std::unique_lock lock(rendezvous_lock);++racing_arrived;rendezvous.notify_all();
            if(!rendezvous.wait_for(lock,std::chrono::seconds(5),[]{return racing_arrived==racing_expected;}))
                throw std::runtime_error("concurrent factory calls did not reach the pin boundary");
        }
        return true;
    }
    std::lock_guard lock(loader_lock);
    if(std::wcscmp(name,L"sl.interposer.dll")!=0)return false;
    ++named_lookups;*result=module_for_name;return true;
}
bool TryUnload(const void* address){
    std::lock_guard lock(loader_lock);
    if(std::find(pinned_addresses.begin(),pinned_addresses.end(),address)!=pinned_addresses.end())return false;
    unloaded_addresses.push_back(address);return true;
}
void* GetProcAddress(HMODULE,const char*);
using MarkerAddress=int(*)(unsigned,const void*);
int FakeMarker(unsigned,const void*);
int OtherMarker(unsigned,const void*);
int MarkerBody(unsigned,const void*);
int OtherMarkerBody(unsigned,const void*);
std::atomic<MarkerAddress> patched_marker{nullptr},patched_other{nullptr};
MarkerAddress created_marker=nullptr,created_other=nullptr;
enum MH_STATUS {MH_OK,MH_ERROR_ALREADY_INITIALIZED,MH_ERROR_ENABLED,MH_ERROR_MEMORY_ALLOC,MH_ERROR_ALREADY_CREATED};
std::atomic<unsigned> creates{0},enables{0};
void* foreign_target=nullptr;
void* failed_enable_target=nullptr;
MH_STATUS MH_Initialize(){return MH_OK;}
MH_STATUS MH_CreateHook(void* target,void* detour,void** original){
    ++creates;if(target==foreign_target)return MH_ERROR_ALREADY_CREATED;
    // Model the global entry patch and its separate trampoline, so a native
    // pointer cached before installation also reaches the production detour.
    if(target==reinterpret_cast<void*>(&FakeMarker)){
        created_marker=reinterpret_cast<MarkerAddress>(detour);*original=reinterpret_cast<void*>(&MarkerBody);
    }else if(target==reinterpret_cast<void*>(&OtherMarker)){
        created_other=reinterpret_cast<MarkerAddress>(detour);*original=reinterpret_cast<void*>(&OtherMarkerBody);
    }else *original=target;
    return MH_OK;
}
MH_STATUS MH_EnableHook(void* target){
    ++enables;if(target==failed_enable_target)return MH_ERROR_MEMORY_ALLOC;
    if(target==reinterpret_cast<void*>(&FakeMarker))patched_marker.store(created_marker);
    if(target==reinterpret_cast<void*>(&OtherMarker))patched_other.store(created_other);
    return MH_OK;
}
namespace nr_processing_runtime {std::atomic<bool> work_enabled{true};}
nr_fg_presentation::Policy g_fg_presentation_policy;
std::atomic<unsigned> g_history_generation{1};
namespace adapter {
using nr_source_frame::Id;using nr_source_frame::Source;
SRWLOCK source_lock=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&source_lock);}~Lock(){ReleaseSRWLockExclusive(&source_lock);}};
thread_local Source sl_source{},sl_present_source{};
Source PublishStreamlineSource(Id provider,Id epoch,unsigned view,unsigned frame){return {provider^((Id(view)+1)<<32),epoch,Id(frame)+1};}
struct StreamlineScope {Source previous;explicit StreamlineScope(Source value):previous(sl_source){sl_source=value;}~StreamlineScope(){sl_source=previous;}};
#ifndef NR_STREAMLINE_IMPLEMENTATION
#define NR_STREAMLINE_IMPLEMENTATION "../src/nr_fg_streamline.inl"
#endif
#include NR_STREAMLINE_IMPLEMENTATION
// Compile the exact selection chain from the Feature11 EvaluateScope. Only
// producer/resource plumbing is doubled; the preference order is production.
Source ProducerSource(Id handle,Id generation,Id frame){return {handle,generation,frame};}
Source SelectFgSource(nr_source_frame::Label inherited={},Id frame=0){
    struct Inputs {Id fg_frame;} inputs{frame};
    const void* handle=reinterpret_cast<const void*>(std::uintptr_t(0x20000));
    Id generation=7;Source source{};
#include "production_fg_selection.inl"
    return source;
}
}
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
int FakeConstants(const void*,const void*,const void*){return 0;}
int FakeTags(const void*,const void*,const void*,unsigned,void*){return 0;}
int FakeEvaluate(unsigned,const void*,const void*const*,unsigned,void*){return 0;}
int marker_result=0,factory_result=0;
std::atomic<unsigned> marker_calls{0},other_marker_calls{0},factory_calls{0};
thread_local unsigned seen_marker=UINT_MAX,seen_feature=UINT_MAX;
thread_local const void* seen_frame=nullptr;
thread_local const char* seen_name=nullptr;
int MarkerBody(unsigned value,const void* frame){++marker_calls;seen_marker=value;seen_frame=frame;return marker_result;}
int OtherMarkerBody(unsigned value,const void* frame){++other_marker_calls;seen_marker=value;seen_frame=frame;return 137;}
int FakeMarker(unsigned value,const void* frame){const auto patch=patched_marker.load();return patch?patch(value,frame):MarkerBody(value,frame);}
int OtherMarker(unsigned value,const void* frame){const auto patch=patched_other.load();return patch?patch(value,frame):OtherMarkerBody(value,frame);}
thread_local void* factory_target=reinterpret_cast<void*>(&FakeMarker);
int FakeGetFunction(unsigned feature,const char* name,void*& function){
    ++factory_calls;seen_feature=feature;seen_name=name;function=factory_target;return factory_result;
}
int FakeFgOptions(const void*,const void*){return 0;}
void* GetProcAddress(HMODULE module,const char* name){
    if(module!=provider_module)return nullptr;
    if(std::strcmp(name,"slSetConstants")==0)return reinterpret_cast<void*>(&FakeConstants);
    if(std::strcmp(name,"slSetTagForFrame")==0)return reinterpret_cast<void*>(&FakeTags);
    if(std::strcmp(name,"slEvaluateFeature")==0)return reinterpret_cast<void*>(&FakeEvaluate);
    if(std::strcmp(name,"slGetFeatureFunction")==0)return reinterpret_cast<void*>(&FakeGetFunction);
    return nullptr;
}

void FactoryWhileInstalling(){
    using namespace adapter::sl_wire;
    AcquireSRWLockExclusive(&install_lock);
    const auto attempts=blocking_install_attempts.load();
    void* function=nullptr;
    try {
        Check(HookGetFunction(3,"slPCLSetMarker",function)==0,"factory result preserved");
        Check(function==reinterpret_cast<void*>(&FakeMarker)&&marker_target.load()==&FakeMarker,"one-time factory query records its native target");
        Check(blocking_install_attempts==attempts,"factory never waits for the install lock");
        Check(creates==0&&enables==0,"factory never patches or freezes another thread");
    }catch(...){ReleaseSRWLockExclusive(&install_lock);throw;}
    ReleaseSRWLockExclusive(&install_lock);
}
void LoaderInversion(){
    using namespace adapter::sl_wire;
    signal_poll=true;
    std::unique_lock loader(loader_lock);
    std::exception_ptr poll_error,callback_error;
    std::thread polling([&]{try{Poll();}catch(...){poll_error=std::current_exception();}});
    {
        std::unique_lock lock(rendezvous_lock);
        if(!rendezvous.wait_for(lock,std::chrono::seconds(5),[]{return poll_waiting_loader;})){
            loader.unlock();polling.join();throw std::runtime_error("Poll never reached module resolution");
        }
    }
    // A plugin may request an already-loaded feature function from a loader
    // callback. Poll is now holding install_lock and waiting for that loader.
    void* function=nullptr;
    try {
        Check(HookGetFunction(3,"slPCLSetMarker",function)==0,"loader callback preserves factory result");
        Check(function==reinterpret_cast<void*>(&FakeMarker)&&marker_target.load()==&FakeMarker,"loader callback preserves the native callable");
        Check(creates==0&&enables==0,"factory callback does not install hooks");
    }catch(...){callback_error=std::current_exception();}
    loader.unlock();polling.join();
    if(callback_error)std::rethrow_exception(callback_error);
    if(poll_error)std::rethrow_exception(poll_error);
    Check(creates==5&&enables==5,"Poll alone installs four exports and the deferred marker");
    Check(Ready(reinterpret_cast<void**>(&get_function)),"factory hook is enabled after loader release");
    Check(blocking_install_attempts==0,"no blocking install acquisition on either production entry");
}
struct Token {adapter::sl_wire::Frame header{};unsigned value=0;};
unsigned FrameSequence(const void* pointer){return static_cast<const Token*>(pointer)->value;}
void Contracts(){
    using namespace adapter;using namespace adapter::sl_wire;
    const auto cached_before_install=&FakeMarker;
    void* function=nullptr;
    factory_result=-71;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==-71&&function==factory_target&&!marker_target.load(),
        "failed factory return and output pointer are preserved");
    factory_result=0;factory_target=nullptr;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&!function&&!marker_target.load(),"null factory target stays native");
    factory_target=reinterpret_cast<void*>(&FakeMarker);
    Check(HookGetFunction(3,nullptr,function)==0&&function==factory_target&&!marker_target.load(),"null function name stays native");
    Check(HookGetFunction(3,"slPCLUnrelated",function)==0&&function==factory_target&&!marker_target.load(),"unrelated function stays native");
    enabled=false;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&!marker_target.load(),"disabled adapter does not record a target");
    enabled=true;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target,"successful factory pointer stays native");
    Check(seen_feature==3&&std::strcmp(seen_name,"slPCLSetMarker")==0,"factory arguments are forwarded exactly");
    Check(marker_target.load()==&FakeMarker&&!marker,"deferred target does not pretend the hook is installed");
    Check(pinned_addresses.size()==2&&pinned_addresses[0]==reinterpret_cast<void*>(&HookMarker)&&
        pinned_addresses[1]==factory_target,"detour and actual plugin target are pinned before deferral");
    const auto cached_factory_result=reinterpret_cast<MarkerFn>(function);
    const auto pins=pinned_addresses.size();
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&
        pinned_addresses.size()==pins,"same target reuses registration without loader calls");
    Check(creates==0&&enables==0,"factory only records the pending target");

    void* table[]={reinterpret_cast<void*>(&FrameSequence)};
    Token token{};token.header.vtable=table;token.header.base.type=frame_type;token.header.base.version=1;token.value=9;
    Viewport viewport{};viewport.base.type=viewport_type;viewport.base.version=1;viewport.value=7;
    provider=provider_module;constants=FakeConstants;tags=FakeTags;evaluate=FakeEvaluate;
    HookConstants(nullptr,&token,&viewport);const auto source=OneView(&token);
    Check(bool(source)&&source.epoch==1&&source.sequence==10,"source stream epoch and token identity are unchanged");
    Check(cached_factory_result(4,&token)==0&&!sl_present_source&&!SelectFgSource(),
        "before installation native execution creates no speculative Present lease");
    Poll();
    Check(creates==5&&enables==5&&Ready(reinterpret_cast<void**>(&marker)),"one factory registration is installed by the next available Poll");
    Check(marker==&MarkerBody,"only MinHook supplies the owned trampoline");
    Check(cached_factory_result(4,&token)==0&&sl_present_source==source&&seen_marker==4&&seen_frame==&token&&SelectFgSource()==source,
        "successful PresentStart forwards exact args and reaches the production FG source selector");
    marker_result=-29;
    Check(cached_before_install(5,&token)==-29&&SelectFgSource()==source,"failed marker preserves native result and active scope");
    marker_result=0;nr_processing_runtime::work_enabled=false;
    Check(cached_before_install(5,&token)==0&&!sl_present_source&&!SelectFgSource(),
        "pre-install native pointer expires PresentEnd while F6 is off");
    Check(cached_factory_result(4,&token)==0&&!sl_present_source,"F6 off never creates a Present scope");
    nr_processing_runtime::work_enabled=true;cached_factory_result(4,&token);enabled=false;
    Check(!CurrentStreamlinePresentSource(),"disabled observation cannot lend a cached TLS lease");
    Check(cached_before_install(5,&token)==0&&!sl_present_source,"cached native pointer observes end while disabled");
    enabled=true;
    for(unsigned boundary:{11u,20u}){
        cached_factory_result(4,&token);Check(SelectFgSource()==source,"application source restored before out-of-band boundary");
        Check(cached_before_install(boundary,&token)==0&&!sl_present_source&&!SelectFgSource(),"cached native OOB boundary cannot lend old source identity");
    }
    Viewport second=viewport;second.value=8;HookTags(&token,&second,nullptr,0,nullptr);
    Check(cached_factory_result(4,&token)==0&&!sl_present_source,"ambiguous same-token views cannot guess a source");

    // A different plugin's callable used to be rejected by Install while the
    // old TLS lease stayed admissible. Exercise the actual getter AND the
    // actual EvaluateScope selection on a thread that did not see that query.
    token.value=10;HookConstants(nullptr,&token,&viewport);const auto newer=OneView(&token);
    cached_factory_result(4,&token);Check(SelectFgSource()==newer,"old owner has a live lease before target replacement");
    std::exception_ptr callback_error;
    std::thread different([&]{try{
        factory_target=reinterpret_cast<void*>(&OtherMarker);void* returned=nullptr;
        if(HookGetFunction(3,"slPCLSetMarker",returned)!=0||returned!=factory_target)
            throw std::runtime_error("different callable factory result changed");
    }catch(...){callback_error=std::current_exception();}});
    different.join();if(callback_error)std::rethrow_exception(callback_error);
    Check(sl_present_source==newer,"other-thread factory did not rewrite this thread's TLS");
    Check(marker_target.load()==&FakeMarker&&marker==&MarkerBody&&marker_owner_ambiguous.load(),
        "different callable cannot replace the pinned owner or its trampoline");
    Check(!CurrentStreamlinePresentSource()&&!SelectFgSource(),"ambiguous callable invalidates old TLS at the real FG consumer");
    Check(SelectFgSource({},91).sequence==91,"native FG frame fallback remains available after PCL ambiguity");
    sl_source={99,8,2};Check(SelectFgSource()==sl_source,"explicit Streamline Evaluate scope retains its own identity");sl_source={};
    nr_source_frame::Label label{{77,3,6},nr_source_frame::Role::Application};
    Check(SelectFgSource(label)==label.source,"resource lineage retains its independent application identity");
    Check(cached_before_install(4,&token)==0&&!sl_present_source,"old cached callable cannot re-authorize an ambiguous PCL owner");
    Check(OtherMarker(77,nullptr)==137&&other_marker_calls==1,"different callable retains native execution");
    factory_target=reinterpret_cast<void*>(&FakeFgOptions);
    Check(HookGetFunction(1000,"slDLSSGSetOptions",function)==0&&function==reinterpret_cast<void*>(&HookFgOptions),
        "existing FG option observation remains wired");
    FgOptionsPrefix options{};options.base.type=fg_options_type;options.base.version=5;options.mode=1;
    Check(reinterpret_cast<FgOptionsFn>(function)(&viewport,&options)==0&&g_fg_presentation_policy.Read().late_owned,
        "existing successful FG option policy still observes the application call");
    Check(creates==5&&enables==5&&blocking_install_attempts==0,"factory contracts never change MinHook ownership");
}
void PinFailure(bool observer){
    using namespace adapter::sl_wire;
    fail_pin=observer?reinterpret_cast<void*>(&HookMarker):reinterpret_cast<void*>(&FakeMarker);
    void* function=nullptr;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&!marker_target.load(),
        "failed module pin leaves the original callable and no deferred target");
    Check(reinterpret_cast<MarkerFn>(function)(81,nullptr)==0&&seen_marker==81,"pin failure retains native execution");
    fail_pin=nullptr;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&marker_target.load()==&FakeMarker,"pin failure can retry on a later query");
    Check(!TryUnload(reinterpret_cast<void*>(&FakeMarker))&&!TryUnload(reinterpret_cast<void*>(&HookMarker)),
        "pending target and observer prevent unload or address reuse before Poll");
    Check(creates==0&&enables==0,"pin retry never adopts or creates an inline hook");
    Poll();
    Check(Ready(reinterpret_cast<void**>(&marker))&&reinterpret_cast<MarkerFn>(function)(82,nullptr)==0&&seen_marker==82,
        "deferred hook remains callable after target unload attempt");
}
void UnloadedTarget(){
    using namespace adapter::sl_wire;
    Check(TryUnload(factory_target),"unpinned test target can be unloaded");
    void* function=nullptr;
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&!marker_target.load(),
        "unprovable target lifetime is never deferred");
    unloaded_addresses.clear();
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&marker_target.load()==&FakeMarker,"fresh live target is pinned before registration");
    Check(!TryUnload(factory_target),"registered target cannot be unloaded and reused");
}
void ConcurrentFactories(bool mixed){
    using namespace adapter::sl_wire;
    constexpr unsigned count=12;
    racing_expected=count;racing_target_a=reinterpret_cast<void*>(&FakeMarker);
    racing_target_b=reinterpret_cast<void*>(&OtherMarker);
    std::vector<void*> results(count,nullptr),requested(count,nullptr);
    std::vector<std::exception_ptr> errors(count);
    std::vector<std::thread> callers;
    for(unsigned i=0;i<count;++i)callers.emplace_back([&,i]{try{
        factory_target=mixed&&(i&1)?reinterpret_cast<void*>(&OtherMarker):reinterpret_cast<void*>(&FakeMarker);
        requested[i]=factory_target;
        if(HookGetFunction(3,"slPCLSetMarker",results[i])!=0)throw std::runtime_error("concurrent factory result changed");
    }catch(...){errors[i]=std::current_exception();}});
    for(auto& caller:callers)caller.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    racing_expected=0;
    const auto owner=marker_target.load(std::memory_order_acquire);
    Check(owner==&FakeMarker||(mixed&&owner==&OtherMarker),"one exact target wins registration");
    for(unsigned i=0;i<count;++i){
        Check(results[i]==requested[i],"concurrent factory returns the exact native callable");
        Check(reinterpret_cast<MarkerFn>(results[i])(99,nullptr)==(requested[i]==reinterpret_cast<void*>(&OtherMarker)?137:0),
            "each concurrent result still calls its own original");
    }
    Check(!TryUnload(reinterpret_cast<void*>(owner)),"winning target was pinned before atomic registration");
    Check(marker_owner_ambiguous.load()==mixed,"different concurrent targets retire only the ambiguous PCL evidence");
    Check(creates==0&&enables==0&&blocking_install_attempts==0,"concurrent factories do not acquire installation ownership");
    Poll();Check(creates==5&&enables==5&&Ready(reinterpret_cast<void**>(&marker)),"concurrent queries install exactly one owned marker hook");
}
void PollContracts(bool foreign){
    using namespace adapter::sl_wire;
    enabled=false;Poll();Check(creates==0&&pinned_addresses.empty(),"disabled Poll does no module work");
    enabled=true;nr_processing_runtime::work_enabled=false;Poll();Check(creates==0&&pinned_addresses.empty(),"F6 off Poll does no module work");
    nr_processing_runtime::work_enabled=true;next_poll=GetTickCount64()+1;Poll();Check(creates==0&&pinned_addresses.empty(),"Poll honors its backoff");
    next_poll=0;AcquireSRWLockExclusive(&install_lock);Poll();ReleaseSRWLockExclusive(&install_lock);
    Check(creates==0&&pinned_addresses.empty(),"busy Poll is nonblocking and leaves module state untouched");
    if(foreign)foreign_target=reinterpret_cast<void*>(&FakeGetFunction);
    else failed_enable_target=reinterpret_cast<void*>(&FakeGetFunction);
    Poll();
    Check(!Ready(reinterpret_cast<void**>(&get_function)),"failed or foreign hook is never marked ready");
    Check(creates==4&&enables==(foreign?3u:4u),"Poll retains three successful owned exports");
    foreign_target=nullptr;failed_enable_target=nullptr;next_poll=0;Poll();
    Check(Ready(reinterpret_cast<void**>(&get_function)),"later Poll retries the remaining export");
    Check(creates==(foreign?5u:4u)&&enables==(foreign?4u:5u),"retry does not recreate an owned disabled hook");
    const auto pins=pinned_addresses.size();next_poll=0;Poll();
    Check(pinned_addresses.size()==pins,"ready Poll avoids repeated module pinning");
    void* function=nullptr;const auto before=creates.load();
    Check(HookGetFunction(3,"slPCLSetMarker",function)==0&&function==factory_target&&creates==before,
        "factory only registers the marker after the four exports are ready");
    const auto resolved=named_lookups;module_for_name=reinterpret_cast<HMODULE>(std::uintptr_t(0x30000));
    Poll();
    Check(Ready(reinterpret_cast<void**>(&marker))&&creates==before+1,
        "ready exports and an existing backoff never swallow a single deferred marker query");
    Check(provider==provider_module&&named_lookups==resolved,"marker retry retains the pinned provider identity instead of re-resolving its filename");
}
void MarkerInstallFailure(bool foreign){
    using namespace adapter::sl_wire;
    Poll();void* function=nullptr;HookGetFunction(3,"slPCLSetMarker",function);
    if(foreign)foreign_target=factory_target;else failed_enable_target=factory_target;
    Poll();
    Check(!Ready(reinterpret_cast<void**>(&marker))&&!patched_marker.load(),"failed deferred marker stays uninstalled");
    Check(creates==5&&enables==(foreign?4u:5u),"failed deferred marker does not adopt a foreign trampoline");
    const auto before=creates.load();const auto previous_enables=enables.load();
    Poll();HookGetFunction(3,"slPCLSetMarker",function);Poll();
    Check(creates==before&&enables==previous_enables,"repeated factory queries do not defeat installation failure backoff");
    foreign_target=nullptr;failed_enable_target=nullptr;next_poll=0;Poll();
    Check(Ready(reinterpret_cast<void**>(&marker))&&creates==(foreign?6u:5u)&&enables==(foreign?5u:6u),
        "deferred marker retries safely without recreating an owned hook");
}
int main(int argc,char** argv){try{
    using namespace adapter::sl_wire;
    observed_install=&install_lock;enabled=true;get_function=FakeGetFunction;
    const std::string mode=argc>1?argv[1]:"--reentrant";
    if(mode=="--reentrant")FactoryWhileInstalling();
    else if(mode=="--loader-cycle")LoaderInversion();
    else if(mode=="--contracts")Contracts();
    else if(mode=="--pin-target")PinFailure(false);
    else if(mode=="--pin-observer")PinFailure(true);
    else if(mode=="--unloaded")UnloadedTarget();
    else if(mode=="--same-target-race")ConcurrentFactories(false);
    else if(mode=="--different-target-race")ConcurrentFactories(true);
    else if(mode=="--foreign-hook")PollContracts(true);
    else if(mode=="--retry-owned-hook")PollContracts(false);
    else if(mode=="--foreign-marker")MarkerInstallFailure(true);
    else if(mode=="--retry-owned-marker")MarkerInstallFailure(false);
    else throw std::runtime_error("unknown test mode");
    std::printf("Streamline factory %s: %u checks passed (Win32/MinHook doubles; no SDK/GPU execution)\n",mode.c_str(),checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
