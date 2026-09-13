#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// A live FG feature indicates possible presentation ownership, not proof of a
// successful interpolation. Explicit successful Off overrides the cached
// handle. Do not use this state as a fence or generated-frame identity.
namespace nr_fg_presentation {
enum class Reason : unsigned { None, OptionsEnabled, LiveHandleModeUnknown, Creating };
struct Snapshot {
    std::uint64_t lifecycle=0;
    unsigned handles=0,reconstruction=0,creating=0,options=0;
    Reason reason=Reason::None;
    bool late_owned=false,inline_reconstruction=false;
};
class Policy {
    struct View {unsigned id=0,mode=0;bool used=false;};
    std::mutex mutex_;
    std::array<View,16> views_{};
    bool overflow_=false;
    std::atomic<std::uint64_t> lifecycle_{1};
    std::atomic<unsigned> options_{0},handles_{0},reconstruction_{0},creating_{0};
    void ForgetOptionsLocked() noexcept {
        views_={};overflow_=false;options_.store(0,std::memory_order_release);
    }
    void AdvanceLifecycleLocked() noexcept {
        const auto value=lifecycle_.load(std::memory_order_relaxed);
        lifecycle_.store(value==UINT64_MAX?1:value+1,std::memory_order_release);
    }
public:
    std::uint64_t Lifecycle() const noexcept {return lifecycle_.load(std::memory_order_acquire);}
    void Inventory(unsigned fg,unsigned reconstruction) {
        std::lock_guard lock(mutex_);
        reconstruction_.store(reconstruction,std::memory_order_release);
        const auto previous=handles_.exchange(fg,std::memory_order_acq_rel);
        // A successful option belongs to the Feature11 lifetime in which it
        // was observed. The final Release retires both On and Off evidence.
        // Options-only observation before the first handle remains useful.
        if(previous&& !fg){AdvanceLifecycleLocked();ForgetOptionsLocked();}
    }
    void BeginCreate() {
        const auto previous=creating_.fetch_add(1,std::memory_order_acq_rel);
        if(!previous){std::lock_guard lock(mutex_);AdvanceLifecycleLocked();ForgetOptionsLocked();}
    }
    void EndCreate() noexcept {creating_.fetch_sub(1,std::memory_order_acq_rel);}
    bool Options(unsigned viewport,unsigned mode,bool succeeded,std::uint64_t lifecycle=0) {
        if(!succeeded||mode>3||viewport==UINT32_MAX)return false;
        if(!lifecycle)lifecycle=Lifecycle();
        if(lifecycle!=Lifecycle())return false;
        std::lock_guard lock(mutex_);
        if(lifecycle!=lifecycle_.load(std::memory_order_acquire))return false;
        View* slot=nullptr;
        for(auto& v:views_){if(v.used&&v.id==viewport){slot=&v;break;}if(!v.used&&!slot)slot=&v;}
        if(slot)*slot={viewport,mode,true};else overflow_=true;
        bool active=overflow_;
        for(const auto& v:views_)active|=v.used&&v.mode!=0;
        options_.store(active?2u:1u,std::memory_order_release);return true;
    }
    Snapshot Read() const noexcept {
        Snapshot s{lifecycle_.load(std::memory_order_acquire),handles_.load(std::memory_order_acquire),reconstruction_.load(std::memory_order_acquire),
            creating_.load(std::memory_order_acquire),options_.load(std::memory_order_acquire)};
        s.reason=s.creating?Reason::Creating:s.options==2?Reason::OptionsEnabled:
            (s.handles&&s.options!=1)?Reason::LiveHandleModeUnknown:Reason::None;
        s.late_owned=s.reason!=Reason::None;
        s.inline_reconstruction=s.late_owned&&s.reconstruction!=0;return s;
    }
};
struct Creating {
    Policy& policy;bool active;
    Creating(Policy& p,bool enabled):policy(p),active(enabled){if(active)policy.BeginCreate();}
    ~Creating(){if(active)policy.EndCreate();}
    Creating(const Creating&)=delete;Creating& operator=(const Creating&)=delete;
};
inline const char* Name(Reason r) noexcept {
    switch(r){case Reason::OptionsEnabled:return "FG-options-enabled";
    case Reason::LiveHandleModeUnknown:return "FG-live-handle-mode-unobserved";
    case Reason::Creating:return "FG-create-in-progress";default:return "no-FG-present-owner";}
}
}
