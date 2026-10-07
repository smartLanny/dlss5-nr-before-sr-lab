#pragma once
// Private Feature1 lifecycle; original/trampoline API and parameter factory are
// supplied by the Core. No second NGX Init/Shutdown or independent NR owner.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace nr_private_sr {
using Id=std::uint64_t;using Result=std::uint32_t;
constexpr Result success=1;constexpr Id removed=std::numeric_limits<Id>::max();
struct Recording {
    Id device=0,queue=0,list=0,lifetime=0,reset=0;
    explicit operator bool()const noexcept{return device&&queue&&list&&lifetime&&reset;}
    friend bool operator==(const Recording&,const Recording&)=default;
};
using RecordingReleased=bool(*)(void*,Recording);
struct Source {
    Id stream=0,epoch=0,sequence=0;
    explicit operator bool()const noexcept{return stream&&epoch&&sequence;}
    friend bool operator==(const Source&,const Source&)=default;
};
struct Fence {
    Id queue=0,object=0,value=0;
    explicit operator bool()const noexcept{return queue&&object&&value&&value!=removed;}
    friend bool operator==(const Fence&,const Fence&)=default;
};
struct Completion {Id queue=0,object=0,value=0;};
inline bool Complete(Fence target,Completion done)noexcept {
    return bool(target)&&done.queue==target.queue&&done.object==target.object&&done.value!=removed&&done.value>=target.value;
}
struct Shape {
    unsigned render_width=0,render_height=0,output_width=0,output_height=0;
    int quality=0;unsigned create_flags=0;Id device=0,queue=0,generation=0;
    friend bool operator==(const Shape&,const Shape&)=default;
    bool Valid()const noexcept {
        return device&&queue&&generation&&render_width&&render_height&&output_width&&output_height&&
            render_width<=16384&&render_height<=16384&&output_width<=16384&&output_height<=16384&&
            render_width<=output_width&&render_height<=output_height&&quality>=0&&quality<=5;
    }
};
struct Frame {
    Recording recording{};Source source{},motion_reference{};
    void *color=nullptr,*depth=nullptr,*motion=nullptr,*output=nullptr;
    float jitter_x=0,jitter_y=0,mv_x=0,mv_y=0;bool reset=true;
    bool resource_contract_verified=false;
    bool Valid(const Shape& s)const noexcept {
        return bool(recording)&&bool(source)&&recording.device==s.device&&recording.queue==s.queue&&
            resource_contract_verified&&color&&depth&&motion&&output&&color!=output&&depth!=output&&motion!=output&&
            std::isfinite(jitter_x)&&std::isfinite(jitter_y)&&std::isfinite(mv_x)&&std::isfinite(mv_y);
    }
};
struct Api {
    void* user=nullptr;
    Result (*allocate)(void*,void**)=nullptr;
    Result (*destroy)(void*,void*)=nullptr;
    void (*set_u)(void*,void*,const char*,unsigned)=nullptr;
    void (*set_i)(void*,void*,const char*,int)=nullptr;
    void (*set_f)(void*,void*,const char*,float)=nullptr;
    void (*set_r)(void*,void*,const char*,void*)=nullptr;
    Result (*create)(void*,void*,int,void*,void**)=nullptr;
    Result (*evaluate)(void*,void*,void*,void*)=nullptr;
    Result (*release)(void*,void*)=nullptr;
    void (*retain_resource)(void*,void*)=nullptr;
    void (*release_resource)(void*,void*)=nullptr;
    Completion (*poll)(void*,Fence)=nullptr;
    void (*retain_fence)(void*,Id)=nullptr;
    void (*release_fence)(void*,Id)=nullptr;
    RecordingReleased recording_released=nullptr;void* retirement_user=nullptr;
    bool original_sr_owner_verified=false;
    bool Valid()const noexcept {
        return original_sr_owner_verified&&allocate&&destroy&&set_u&&set_i&&set_f&&set_r&&create&&evaluate&&release&&
            retain_resource&&release_resource&&poll&&retain_fence&&release_fence;
    }
};
enum class Status {Empty,Ready,CreatedNeedsSubmission,AwaitCreateCompletion,EvaluateRecorded,Busy,
    InvalidContract,RebuildRequired,DuplicateSource,Poisoned,Retired};
struct Outcome {Status status=Status::Empty;Result result=0;bool recorded=false,evaluated=false;};
// CPU-only observation of the value actually passed to Feature1 Bind. A
// caller's reset request alone cannot describe this private history.
enum ResetCause:unsigned { RequestedReset=1u, HistoryReset=2u, PredecessorMismatch=4u };
enum HistoryCause:unsigned { InitialHistory=1u, RecordingBackpressure=2u,
    SlotPressure=4u, IdentityChanged=8u, ComposeFailed=16u, Cancelled=32u };
struct BindEvidence {Source predecessor{};unsigned reset_causes=0,history_causes=0;bool bound=false;};
class Context {
    struct Flight {
        void* params=nullptr;Recording recording{};Fence fence{};Source source{};
        std::array<void*,4> resources{};bool pending=false,is_create=false;
    };
    Api api_{};Shape shape_{};void* create_params_=nullptr;void* feature_=nullptr;
    std::array<Flight,3> flights_{};Source last_source_{};
    Fence creation_fence_{};Recording creation_recording_{};
    bool initialized_=false,created_=false,poisoned_=false,history_reset_=true,creation_completed_=false;
    unsigned history_causes_=InitialHistory;
    Result last_result_=0;
    BindEvidence last_bind_{};
    void Poison(Result r=0)noexcept{poisoned_=true;last_result_=r;}
    void Pin(Flight& f,const Frame& frame,bool create) {
        f.recording=frame.recording;f.source=frame.source;f.pending=true;f.fence={};f.is_create=create;
        f.resources={frame.color,frame.depth,frame.motion,frame.output};
        for(void* p:f.resources)api_.retain_resource(api_.user,p);
    }
    void Unpin(Flight& f) {
        for(void*& p:f.resources){if(p)api_.release_resource(api_.user,p);p=nullptr;}
        if(f.fence)api_.release_fence(api_.user,f.fence.object);
        f.recording={};f.source={};f.pending=false;f.fence={};f.is_create=false;
    }
    void Bind(void* p,const Frame& f,bool reset) {
        api_.set_r(api_.user,p,"Color",f.color);api_.set_r(api_.user,p,"Depth",f.depth);
        api_.set_r(api_.user,p,"MotionVectors",f.motion);api_.set_r(api_.user,p,"Output",f.output);
        api_.set_r(api_.user,p,"ExposureTexture",nullptr);
        api_.set_f(api_.user,p,"DLSS.Pre.Exposure",1.f);api_.set_f(api_.user,p,"DLSS.Exposure.Scale",1.f);
        api_.set_f(api_.user,p,"Jitter.Offset.X",f.jitter_x);api_.set_f(api_.user,p,"Jitter.Offset.Y",f.jitter_y);
        api_.set_f(api_.user,p,"MV.Scale.X",f.mv_x);api_.set_f(api_.user,p,"MV.Scale.Y",f.mv_y);
        api_.set_i(api_.user,p,"Reset",reset?1:0);
        api_.set_u(api_.user,p,"DLSS.Render.Subrect.Dimensions.Width",shape_.render_width);
        api_.set_u(api_.user,p,"DLSS.Render.Subrect.Dimensions.Height",shape_.render_height);
        for(const char* k:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y",
            "DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
            "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y",
            "DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})api_.set_u(api_.user,p,k,0);
    }
    bool Distinct(void* p)const noexcept {
        if(!p||p==create_params_)return false;
        for(const auto& f:flights_)if(p==f.params)return false;return true;
    }
public:
    Context()=default;Context(const Context&)=delete;Context& operator=(const Context&)=delete;
    // Destruction must be explicit. Uncertain vendor state is quarantined.
    bool Initialize(Api api,Shape shape) {
        if(initialized_||poisoned_||!api.Valid()||!shape.Valid())return false;
        api_=api;shape_=shape;initialized_=true;
        void* p=nullptr;Result r=api_.allocate(api_.user,&p);
        if(r!=success||!p){create_params_=p;Poison(r);return false;}
        create_params_=p;
        for(auto& f:flights_){p=nullptr;r=api_.allocate(api_.user,&p);
            if(r!=success||!Distinct(p)){Poison(r);return false;}f.params=p;}
        api_.set_u(api_.user,create_params_,"CreationNodeMask",1);
        api_.set_u(api_.user,create_params_,"VisibilityNodeMask",1);
        api_.set_u(api_.user,create_params_,"Width",shape.render_width);
        api_.set_u(api_.user,create_params_,"Height",shape.render_height);
        api_.set_u(api_.user,create_params_,"OutWidth",shape.output_width);
        api_.set_u(api_.user,create_params_,"OutHeight",shape.output_height);
        api_.set_i(api_.user,create_params_,"PerfQualityValue",shape.quality);
        api_.set_i(api_.user,create_params_,"DLSS.Feature.Create.Flags",static_cast<int>(shape.create_flags));
        return true;
    }
    bool Collect() {
        if(poisoned_)return false;
        for(auto& f:flights_){if(!f.fence)continue;const auto done=api_.poll(api_.user,f.fence);
            if(done.value==removed){Poison();return false;}
            if(Complete(f.fence,done)){if(f.is_create)creation_completed_=true;
                if(!api_.recording_released||api_.recording_released(api_.retirement_user,f.recording))Unpin(f);}}
        return true;
    }
    Outcome Record(void* list,const Frame& frame,const Shape& shape) {
        last_bind_={};
        if(poisoned_)return {Status::Poisoned,last_result_};
        if(!initialized_||!list||reinterpret_cast<Id>(list)!=frame.recording.list||!frame.Valid(shape_))return {Status::InvalidContract};
        if(!(shape==shape_))return {Status::RebuildRequired};
        if(last_source_.stream==frame.source.stream&&last_source_.epoch==frame.source.epoch&&
           frame.source.sequence<=last_source_.sequence)return {Status::DuplicateSource};
        if(!Collect())return {Status::Poisoned,last_result_};
        if(created_&&!creation_completed_) {
            if(!creation_fence_)return {Status::AwaitCreateCompletion};
            const auto done=api_.poll(api_.user,creation_fence_);
            if(done.value==removed){Poison();return {Status::Poisoned};}
            if(!Complete(creation_fence_,done))return {Status::AwaitCreateCompletion};
        }
        // CPU recording order is not GPU submission order. Do not advance one
        // feature's history into a second list while its predecessor can still
        // be submitted after it. Already-submitted work may remain in flight;
        // this is backpressure, not a CPU fence wait or global serialization.
        for(const auto& f:flights_)if(f.pending&&!f.fence){ResetHistory(RecordingBackpressure);return {Status::Busy};}
        Flight* slot=nullptr;for(auto& f:flights_)if(!f.pending&&!f.fence){slot=&f;break;}
        if(!slot){ResetHistory(SlotPressure);return {Status::Busy};}
        Pin(*slot,frame,!created_);
        if(!created_){
            last_bind_={last_source_,RequestedReset|HistoryReset,history_causes_,true};
            Bind(create_params_,frame,true);
            void* handle=nullptr;last_result_=api_.create(api_.user,list,1,create_params_,&handle);
            feature_=handle;creation_recording_=frame.recording;
            if(last_result_!=success||!feature_){Poison(last_result_);return {Status::Poisoned,last_result_,true,false};}
            created_=true;return {Status::CreatedNeedsSubmission,last_result_,true,false};
        }
        const bool matched=bool(frame.motion_reference)&&frame.motion_reference==last_source_;
        const unsigned causes=(frame.reset?RequestedReset:0u)|(history_reset_?HistoryReset:0u)|
            (!matched?PredecessorMismatch:0u);
        last_bind_={last_source_,causes,history_reset_?history_causes_:0u,true};
        Bind(slot->params,frame,causes!=0);
        last_result_=api_.evaluate(api_.user,list,feature_,slot->params);
        if(last_result_!=success){Poison(last_result_);return {Status::Poisoned,last_result_,true,false};}
        last_source_=frame.source;history_reset_=false;history_causes_=0;
        return {Status::EvaluateRecorded,last_result_,true,true};
    }
    bool Submitted(Recording r,Fence fence,bool execute_and_signal_succeeded) {
        bool found=false;
        for(auto& f:flights_)if(f.pending&&f.recording==r){
            found=true;
            if(!execute_and_signal_succeeded||!fence||fence.queue!=r.queue||f.fence){Poison();return false;}
            f.fence=fence;api_.retain_fence(api_.user,fence.object);
            if(f.is_create)creation_fence_=fence;
        }
        return found&&!poisoned_;
    }
    void Discarded(Recording r) {
        for(const auto& f:flights_)if(f.pending&&f.recording==r){Poison();return;}
    }
    void ResetHistory(unsigned reason=IdentityChanged)noexcept{history_reset_=true;history_causes_|=reason;}
    void MarkUncertain()noexcept{Poison();}
    bool ReadyToRetire() {
        if(!initialized_)return true;if(!Collect())return false;
        for(const auto& f:flights_)if(f.pending||f.fence)return false;return true;
    }
    bool Retire() {
        if(!initialized_)return true;if(!Collect())return false;
        for(const auto& f:flights_)if(f.pending||f.fence)return false;
        if(feature_){const auto r=api_.release(api_.user,feature_);if(r!=success){Poison(r);return false;}feature_=nullptr;}
        for(auto& f:flights_)if(f.params){const auto r=api_.destroy(api_.user,f.params);if(r!=success){Poison(r);return false;}f.params=nullptr;}
        if(create_params_){const auto r=api_.destroy(api_.user,create_params_);if(r!=success){Poison(r);return false;}create_params_=nullptr;}
        initialized_=false;created_=false;creation_completed_=false;creation_fence_={};creation_recording_={};
        last_source_={};history_reset_=true;history_causes_=InitialHistory;return true;
    }
    bool Poisoned()const noexcept{return poisoned_;}
    Source LastSource()const noexcept{return last_source_;}
    BindEvidence LastBindEvidence()const noexcept{return last_bind_;}
    const Shape& Configuration()const noexcept{return shape_;}
};
}
