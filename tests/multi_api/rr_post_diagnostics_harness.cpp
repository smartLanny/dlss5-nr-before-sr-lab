// Portable control-flow test of the actual production includes.
// D3D12/NGX/Win32 boundaries below are test doubles, not GPU/game validation.
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../../src/interop/nr_post_compat.h"

using ULONGLONG = unsigned long long;
using NgxResult = std::int32_t;
constexpr NgxResult kNgxSuccess = 1;
struct ID3D12GraphicsCommandList {};
struct NgxHandle {};
struct NgxParams { unsigned output_sentinel = 12345; };
struct FeatureRecord { const NgxHandle* handle; int feature_id; ULONGLONG generation; };
using EvaluateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList*, const NgxHandle*, const NgxParams*, void*);
struct ChainConfig { bool enabled=true, identity_use_original=false; float post_transfer_strength=1; int mode=2; };
ChainConfig g_config;
int g_chain_cs = 0;
wchar_t g_ini_path[] = L"";
std::atomic<ULONGLONG> g_config_generation{0}, g_history_generation{0};
std::atomic<int> g_last_carrier_feature{0};
std::atomic<bool> g_rr_guard_was_active{false}, g_feature_tracking_uncertain{false}, g_lifecycle_tracking_uncertain{false};
std::atomic<ULONGLONG> g_rr_passthrough_evaluations{0}, g_carrier_evaluations{0}, g_pipeline_eligible_carriers{0};
std::atomic<ULONGLONG> g_all_evaluations{0}, g_rr_guarded_sr_evaluations{0};
namespace nr_f8 { std::atomic<bool> g_config_hold{false}; }

enum class Fault {
    None, BeforeState, Prepare, AfterState, Finish, Generation, Handle, Feature,
    Native, UncertainAfter, OtherOwnerAfter, RrDisabled, MasterDisabled, LockBusy,
    OwnershipBusy, Nested, NullList, NullParams, NoGeneration, FeatureUncertain,
    LifecycleUncertain, OtherOwnerBefore, ZeroStrength, IdentityZero,
    BeforeAndNative
};
Fault fault = Fault::None;
unsigned native_calls=0, scope_calls=0, prepare_calls=0, finish_calls=0, restores=0, quarantines=0;
bool lock_held=false, before_live=false, after_live=false, owner_released=false;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
ULONGLONG GetTickCount64() { return 10000; }
template<class... T> void Log(const char*, T...) {}
bool TryEnterCriticalSection(int*) { lock_held=fault!=Fault::LockBusy; return lock_held; }
void EnterCriticalSection(int*) { lock_held=true; }
void LeaveCriticalSection(int*) { Require(lock_held,"unbalanced chain lock"); lock_held=false; }
unsigned GetPrivateProfileIntW(const wchar_t*, const wchar_t*, unsigned fallback, const wchar_t*) { return fallback; }
bool WriteIniInt(const wchar_t*, int) { return true; }
void ApplyConfig(ChainConfig c, const char*) { g_config=c; }
#include "../../src/interop/nr_post_settings.inl"

enum class BypassReason { RayReconstructionGuard, Feature1TransactionBusy };
void SetBypassReason(BypassReason) {}
bool OtherNrOwnerActive() {
    return fault==Fault::OtherOwnerBefore || (fault==Fault::OtherOwnerAfter && native_calls!=0);
}
void TrackDlssResolution(const NgxParams*, const FeatureRecord&) {}
void FeedbackObserveEvaluate(const NgxParams*, const FeatureRecord&, ULONGLONG) {}
void BeginPipelineStatsScopeLocked(ULONGLONG, int mode) { Require(mode==1,"RR must remain post"); }
struct ScopedRrOwnershipGate {
    ScopedRrOwnershipGate(bool, bool, bool) {}
    bool acquired() const { return fault!=Fault::OwnershipBusy; }
    bool is_nested() const { return fault==Fault::Nested; }
    void Release() { Require(!owner_released,"ownership double release"); owner_released=true; }
};
struct NativeCommandStateScope {
    ID3D12GraphicsCommandList* command_list;
    bool active, before;
    explicit NativeCommandStateScope(ID3D12GraphicsCommandList* list) : command_list(list) {
        before=++scope_calls==1;
        active=before ? (fault!=Fault::BeforeState && fault!=Fault::BeforeAndNative) : fault!=Fault::AfterState;
        if (before) before_live=active; else after_live=active;
    }
    void Restore() {
        if (!active) return;
        ++restores; active=false;
        if (before) before_live=false; else after_live=false;
    }
    ~NativeCommandStateScope() { Restore(); }
};
struct PostSrSubstitution { bool active=false; };
PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList*, const NgxHandle*, const NgxParams*, const FeatureRecord&) {
    Require(before_live && native_calls==0,"prepare must run before native RR");
    ++prepare_calls;
    return {fault!=Fault::Prepare};
}
NgxResult Original(ID3D12GraphicsCommandList*, const NgxHandle*, const NgxParams*, void*) {
    Require(!before_live && !after_live,"do not carry an active state snapshot across native RR");
    Require(++native_calls==1,"native RR evaluated more than once");
    if (fault==Fault::UncertainAfter) g_lifecycle_tracking_uncertain=true;
    return fault==Fault::Native || fault==Fault::BeforeAndNative ? -7 : kNgxSuccess;
}
FeatureRecord FindFeature(const NgxHandle* handle) {
    return {fault==Fault::Handle ? nullptr : handle, fault==Fault::Feature ? 1 : 13,
            fault==Fault::Generation ? 8u : 7u};
}
bool FinishAfterSr(ID3D12GraphicsCommandList*, const NgxParams*, const FeatureRecord&, PostSrSubstitution&) {
    Require(after_live && native_calls==1,"finish must run after native RR");
    ++finish_calls;
    return fault!=Fault::Finish;
}
void QuarantineAbortedRrTransactionLocked(ID3D12GraphicsCommandList*) { ++quarantines; }
#include "../../src/interop/nr_rr_post_runtime.inl"

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void FeedbackAppend(std::string& text, const char* format, ...) {
    char buffer[4096];
    va_list args; va_start(args,format);
    const int n=std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    Require(n>=0 && static_cast<unsigned>(n)<sizeof(buffer),"feedback append overflow");
    text.append(buffer,static_cast<std::size_t>(n));
}
std::string Feedback(const char* prefix) {
    std::string s;
    #include "../../src/interop/nr_post_feedback.inl"
    return s;
}
ULONGLONG Value(const std::string& text, const char* key, const char* prefix) {
    const std::string needle=std::string(prefix)+".post.rr."+key+"=";
    const auto offset=text.find(needle);
    Require(offset!=std::string::npos, ("missing F8 key: "+needle).c_str());
    return std::stoull(text.substr(offset+needle.size()));
}
struct Case {
    Fault fault; const char* name;
    unsigned before, prepare, after, finish, gate, recordings, failures, quarantine;
    unsigned scopes, prepares, finishes, restored, attempts;
};
int main() {
    const Case cases[] = {
        {Fault::None,"success",0,0,0,0,0,1,0,0,2,1,1,2,1},
        {Fault::BeforeState,"before state rejected",1,0,0,0,0,0,0,0,1,0,0,0,1},
        {Fault::Prepare,"preparation declined",0,1,0,0,0,0,0,0,1,1,0,1,1},
        {Fault::AfterState,"after state rejected",0,0,1,0,0,0,0,0,2,1,0,1,1},
        {Fault::Finish,"finish declined",0,0,0,1,0,0,0,0,2,1,1,2,1},
        {Fault::Generation,"generation changed",0,0,0,0,1,0,0,0,1,1,0,1,1},
        {Fault::Handle,"handle retired",0,0,0,0,1,0,0,0,1,1,0,1,1},
        {Fault::Feature,"feature changed",0,0,0,0,1,0,0,0,1,1,0,1,1},
        {Fault::Native,"native failure",0,0,0,0,0,0,1,0,1,1,0,1,1},
        {Fault::UncertainAfter,"lifecycle changed",0,0,0,0,1,0,0,1,1,1,0,1,1},
        {Fault::OtherOwnerAfter,"owner changed",0,0,0,0,1,0,0,1,1,1,0,1,1},
        {Fault::BeforeAndNative,"before reject plus native failure",1,0,0,0,0,0,1,0,1,0,0,0,1},
        {Fault::RrDisabled,"legacy RR disable bit cannot disable automatic route",0,0,0,0,0,1,0,0,2,1,1,2,1},
        {Fault::MasterDisabled,"master disabled",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::LockBusy,"chain busy",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::OwnershipBusy,"ownership busy",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::Nested,"nested",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::NullList,"null list",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::NullParams,"null parameters",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::NoGeneration,"no generation",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::FeatureUncertain,"feature tracking uncertain",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::LifecycleUncertain,"lifecycle tracking uncertain",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::OtherOwnerBefore,"other NR owner",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::ZeroStrength,"zero strength",0,0,0,0,0,0,0,0,0,0,0,0,0},
        {Fault::IdentityZero,"identity at zero strength",0,0,0,0,0,1,0,0,2,1,1,2,1}
    };
    for (int requested_mode : {1,2}) for (const auto& c : cases) {
        try {
            fault=c.fault;
            native_calls=scope_calls=prepare_calls=finish_calls=restores=quarantines=0;
            lock_held=before_live=after_live=owner_released=false;
            g_config={}; g_config.mode=requested_mode;
            g_config.enabled=fault!=Fault::MasterDisabled;
            g_config.identity_use_original=fault==Fault::IdentityZero;
            if (fault==Fault::ZeroStrength || fault==Fault::IdentityZero) g_config.post_transfer_strength=0;
            g_feature_tracking_uncertain=fault==Fault::FeatureUncertain;
            g_lifecycle_tracking_uncertain=fault==Fault::LifecycleUncertain;
            nr_post_settings::ready=true;
            nr_post_settings::bits=100u|(fault==Fault::RrDisabled ? 0u : 256u);
            const auto start=Feedback("start");
            ID3D12GraphicsCommandList list; NgxHandle handle; NgxParams params;
            const FeatureRecord feature{&handle,13,fault==Fault::NoGeneration ? 0u : 7u};
            const auto result=ProcessRrPost(Original, fault==Fault::NullList ? nullptr : &list,
                &handle, fault==Fault::NullParams ? nullptr : &params, nullptr,feature);
            Require(native_calls==1,"native RR must always be called once");
            Require(result==(fault==Fault::Native || fault==Fault::BeforeAndNative ? -7 : 1),"native status changed");
            Require(params.output_sentinel==12345,"native output parameter changed");
            Require(g_config.mode==requested_mode,"user mode rewritten");
            Require(!lock_held && !before_live && !after_live,"scope or lock left active");
            Require(scope_calls==c.scopes && prepare_calls==c.prepares && finish_calls==c.finishes &&
                    restores==c.restored && quarantines==c.quarantine,"unexpected control-flow/lifetime boundary");
            const auto end=Feedback("end");
            const char* keys[]={"beforeStateRejected","prepareDeclined","afterStateRejected","finishDeclined",
                "finishGateRejected","nrRecordings","nativeFailures","attempts"};
            const unsigned wanted[]={c.before,c.prepare,c.after,c.finish,c.gate,c.recordings,c.failures,c.attempts};
            for (unsigned i=0;i<8;++i)
                Require(Value(end,keys[i],"end")-Value(start,keys[i],"start")==wanted[i],
                        ("wrong counter delta: "+std::string(keys[i])).c_str());
            if (c.attempts) {
                const auto stage=Value(end,"lastStage","end");
                Require(stage==(c.recordings ? 2u : c.failures ? 3u : 4u),"legacy stage contract changed");
            }
        } catch (const std::exception& e) {
            std::cerr<<c.name<<" mode="<<requested_mode<<": "<<e.what()<<'\n'; return 1;
        }
    }
    std::cout<<"PASS 50 RR diagnostic/control-flow cases (test doubles; no D3D12/NGX/game execution)\n";
}
