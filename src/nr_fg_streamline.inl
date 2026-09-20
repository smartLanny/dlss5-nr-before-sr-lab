// Public Streamline 2.12 ABI adapter. References:
// NVIDIA-RTX/Streamline@v2.12.0 include/sl_struct.h, sl_core_types.h,
// sl_core_api.h, sl_pcl.h. No SDK settings/tags/options are changed.
namespace sl_wire {
struct Base {const void* next;GUID type;std::size_t version;};
struct Frame {void** vtable;Base base;};
struct Viewport {Base base;unsigned value;};
static_assert(sizeof(Base)==32&&offsetof(Frame,base)==8&&offsetof(Viewport,value)==32);
const GUID frame_type={0x830a0f35,0xdb84,0x4171,{0xa8,0x04,0x59,0xb2,0x06,0x49,0x9b,0x18}};
const GUID viewport_type={0x171b6435,0x9b3c,0x4fc8,{0x99,0x94,0xfb,0xe5,0x25,0x69,0xaa,0xa4}};
using SetConstantsFn=int(*)(const void*,const void*,const void*);
using SetTagsFn=int(*)(const void*,const void*,const void*,unsigned,void*);
using EvaluateFn=int(*)(unsigned,const void*,const void*const*,unsigned,void*);
using GetFunctionFn=int(*)(unsigned,const char*,void*&);
using MarkerFn=int(*)(unsigned,const void*);
using FgOptionsFn=int(*)(const void*,const void*);
std::atomic<FgOptionsFn> fg_options{nullptr};
struct FgOptionsPrefix {Base base;unsigned mode,num_frames;};
static_assert(offsetof(FgOptionsPrefix,mode)==32 && sizeof(FgOptionsPrefix)==40);
const GUID fg_options_type={0xfac5f1cb,0x2dfd,0x4f36,{0xa1,0xe6,0x3a,0x9e,0x86,0x52,0x56,0xc5}};
SetConstantsFn constants=nullptr;SetTagsFn tags=nullptr;EvaluateFn evaluate=nullptr;
GetFunctionFn get_function=nullptr;MarkerFn marker=nullptr;
std::atomic<bool> enabled{false};
SRWLOCK install_lock=SRWLOCK_INIT;
HMODULE provider=nullptr;
std::atomic<ULONGLONG> next_poll{0};
struct Hook {void* target=nullptr;void** original=nullptr;bool enabled=false;};
Hook hooks[5]{};
bool Ready(void** original){for(const auto& hook:hooks)if(hook.original==original)return hook.enabled;return false;}
// Public sl::Constants prefix, versions 1/2 (sl_consts.h v2.12.0).
struct ConstantsPrefix { Base base; float camera_view_to_clip[16]; };
static_assert(offsetof(ConstantsPrefix,camera_view_to_clip)==32 && sizeof(ConstantsPrefix)==96);
const GUID constants_type={0xdcd35ad7,0x4e4a,0x4bad,{0xa9,0x0c,0xe0,0xc4,0x9e,0xb2,0x3a,0xfe}};
nr_rr_projection::Cache projection_cache; // Protected by the source lock.
nr_rr_depth::CurrentInput CopyProjection(const void* values) {
    nr_rr_depth::CurrentInput input;
    if(!values)return input;
    ConstantsPrefix prefix{};SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),values,&prefix,sizeof(prefix),&copied)||copied!=sizeof(prefix)) {
        input.state=nr_rr_depth::InputState::Unreadable;return input;
    }
    if(!IsEqualGUID(prefix.base.type,constants_type)||(prefix.base.version!=1&&prefix.base.version!=2))return input;
    std::memcpy(input.matrix,prefix.camera_view_to_clip,sizeof(input.matrix));
    for(float value:input.matrix)if(!std::isfinite(value)){input.state=nr_rr_depth::InputState::Nonfinite;return input;}
    input.state=nr_rr_depth::Resolve(input.matrix,input.projection)?nr_rr_depth::InputState::Supported:nr_rr_depth::InputState::Unsupported;
    return input;
}
struct View {unsigned view=0,frame=0;Source source{};bool occupied=false;};
View views[16]{};
bool FrameValue(const void* pointer,unsigned& value){
    if(!pointer)return false;const auto& frame=*static_cast<const Frame*>(pointer);
    if(frame.base.version!=1||!IsEqualGUID(frame.base.type,frame_type)||!frame.vtable||!frame.vtable[0])return false;
    // FrameToken declares exactly one virtual function: its public uint32
    // conversion. The MSVC x64 slot is invoked explicitly like Core's NGX ABI.
    using ValueFn=unsigned(*)(const void*);value=reinterpret_cast<ValueFn>(frame.vtable[0])(pointer);return true;
}
bool ViewValue(const void* pointer,unsigned& value){
    if(!pointer)return false;const auto& view=*static_cast<const Viewport*>(pointer);
    if(view.base.version!=1||!IsEqualGUID(view.base.type,viewport_type)||view.value==UINT_MAX)return false;
    value=view.value;return true;
}
Source Remember(const void* token,const void* viewport){
    unsigned frame=0,view=0;if(!FrameValue(token,frame)||!ViewValue(viewport,view)||!provider)return {};
    auto source=PublishStreamlineSource(reinterpret_cast<Id>(provider),1,view,frame);
    Lock lock;View* free=nullptr;for(auto& v:views){if(v.occupied&&v.view==view){v={view,frame,source,true};return source;}
        if(!v.occupied&&!free)free=&v;}
    if(free)*free={view,frame,source,true};return source;
}
Source OneView(const void* token){
    unsigned frame=0;if(!FrameValue(token,frame))return {};Lock lock;
    Source source{};unsigned matches=0;for(const auto& view:views)if(view.occupied&&view.frame==frame){source=view.source;++matches;}
    return matches==1?source:Source{};
}
int HookConstants(const void* values,const void* frame,const void* viewport){
    const auto result=constants(values,frame,viewport);
    if(enabled.load(std::memory_order_relaxed)&&nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)) {
        // A failed call never publishes source identity, but invalidates an
        // earlier same-token camera so it cannot silently survive failure.
        if(result==0){const auto source=Remember(frame,viewport);const auto input=CopyProjection(values);
            Lock lock;projection_cache.Put(source,input,GetTickCount64(),g_history_generation.load());}
        else {unsigned f=0,v=0;if(provider&&FrameValue(frame,f)&&ViewValue(viewport,v)) {
            const auto source=PublishStreamlineSource(reinterpret_cast<Id>(provider),1,v,f);
            Lock lock;projection_cache.Put(source,{},GetTickCount64(),g_history_generation.load());}}
    }
    return result;
}
int HookTags(const void* frame,const void* viewport,const void* resources,unsigned count,void* commands){
    const auto result=tags(frame,viewport,resources,count,commands);
    if(result==0&&enabled.load(std::memory_order_relaxed)&&nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))(void)Remember(frame,viewport);
    return result;
}
int HookEvaluate(unsigned feature,const void* frame,const void*const* inputs,unsigned count,void* commands){
    nr_rr_projection::Evaluation projection_scope;
    if(!enabled.load(std::memory_order_relaxed)||!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return evaluate(feature,frame,inputs,count,commands);
    Source source{};
    if(inputs&&count<=64)for(unsigned i=0;i<count;++i){unsigned viewport=0;
        if(ViewValue(inputs[i],viewport)){source=Remember(frame,inputs[i]);break;}}
    if(!source)source=OneView(frame);
    if(feature==1001 && source && commands){Lock lock;
        projection_scope.Set(source,projection_cache.Get(source,GetTickCount64(),g_history_generation.load()),commands);}
    StreamlineScope scope(source);return evaluate(feature,frame,inputs,count,commands);
}
int HookMarker(unsigned value,const void* frame){
    const auto result=marker(value,frame);
    // End/OOB boundaries also expire a lease while F6 is off, so toggling back
    // on cannot revive an old application Present scope on this thread.
    if(result==0&&(value==5||value==11||value==20))sl_present_source={};
    if(result==0&&enabled.load(std::memory_order_relaxed)&&nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)){
        // Public PCL ePresentStart/ePresentEnd; OOB markers are never promoted.
        if(value==4)sl_present_source=OneView(frame);
    }
    return result;
}
bool Install(void* target,void* detour,void** original){
    if(!target||!detour)return false;
    Hook* selected=nullptr;
    for(auto& hook:hooks){if(hook.original==original){if(hook.target!=target)return false;selected=&hook;break;}
        if(!hook.target&&!selected)selected=&hook;}
    if(!selected)return false;
    if(selected->enabled)return true;
    HMODULE pinned=nullptr,provider_pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(detour),&pinned)||
       !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(target),&provider_pin))return false;
    // Never adopt another tool's trampoline or remove its hook.
    if(!selected->target){if(MH_CreateHook(target,detour,original)!=MH_OK)return false;
        selected->target=target;selected->original=original;}
    const auto result=MH_EnableHook(target);selected->enabled=result==MH_OK||result==MH_ERROR_ENABLED;return selected->enabled;
}
int HookFgOptions(const void* viewport,const void* options){
    const auto original=fg_options.load(std::memory_order_acquire);
    if(!original)return -1; // function is only published after the original target
    const auto lifecycle=g_fg_presentation_policy.Lifecycle();
    const auto result=original(viewport,options);
    unsigned view=0;
    if(result==0&&options&&ViewValue(viewport,view)){
        const auto& value=*static_cast<const FgOptionsPrefix*>(options);
        if(value.base.version>=1&&value.base.version<=5&&IsEqualGUID(value.base.type,fg_options_type))
            (void)g_fg_presentation_policy.Options(view,value.mode,true,lifecycle);
    }
    return result;
}
int HookGetFunction(unsigned feature,const char* name,void*& function){
    const auto result=get_function(feature,name,function);
    if(result==0&&feature==1000&&function&&name&&std::strcmp(name,"slDLSSGSetOptions")==0){
        const auto original=reinterpret_cast<FgOptionsFn>(function);
        FgOptionsFn expected=nullptr;
        if(original!=&HookFgOptions && (fg_options.compare_exchange_strong(expected,original,std::memory_order_acq_rel)||expected==original))
            function=reinterpret_cast<void*>(&HookFgOptions);
        // Observe successful app calls only. No SDK polling/setting changes,
        // and no stop-the-world hook installation inside this factory callback.
    }
    if(result==0&&function&&name&&std::strcmp(name,"slPCLSetMarker")==0&&enabled.load(std::memory_order_relaxed)){
        AcquireSRWLockExclusive(&install_lock);
        (void)Install(function,reinterpret_cast<void*>(&HookMarker),reinterpret_cast<void**>(&marker));
        ReleaseSRWLockExclusive(&install_lock);
    }
    return result;
}
void Poll(){
    if(!enabled.load(std::memory_order_relaxed)||!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;
    const auto now=GetTickCount64();if(now<next_poll.load(std::memory_order_relaxed))return;
    if(!TryAcquireSRWLockExclusive(&install_lock))return;
    next_poll.store(now+1000,std::memory_order_relaxed);
    if(Ready(reinterpret_cast<void**>(&constants))&&Ready(reinterpret_cast<void**>(&evaluate))&&Ready(reinterpret_cast<void**>(&get_function))){ReleaseSRWLockExclusive(&install_lock);return;}
    HMODULE candidate=nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"sl.interposer.dll",&candidate))provider=candidate;
    if(provider){const auto init=MH_Initialize();if(init==MH_OK||init==MH_ERROR_ALREADY_INITIALIZED){
        (void)Install(reinterpret_cast<void*>(GetProcAddress(provider,"slSetConstants")),reinterpret_cast<void*>(&HookConstants),reinterpret_cast<void**>(&constants));
        (void)Install(reinterpret_cast<void*>(GetProcAddress(provider,"slSetTagForFrame")),reinterpret_cast<void*>(&HookTags),reinterpret_cast<void**>(&tags));
        (void)Install(reinterpret_cast<void*>(GetProcAddress(provider,"slEvaluateFeature")),reinterpret_cast<void*>(&HookEvaluate),reinterpret_cast<void**>(&evaluate));
        (void)Install(reinterpret_cast<void*>(GetProcAddress(provider,"slGetFeatureFunction")),reinterpret_cast<void*>(&HookGetFunction),reinterpret_cast<void**>(&get_function));
    }}
    ReleaseSRWLockExclusive(&install_lock);
}
} // namespace sl_wire
void PollStreamline(){sl_wire::Poll();}
