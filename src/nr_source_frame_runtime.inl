#define NR_HAS_ENERGY_GATE 1
// BEGIN ENERGY GATE PLATFORM ADAPTER
#include "nr_energy_gate.h"
namespace nr_processing_runtime {
// All state changes and admissions use this small CPU-only lock. No GPU wait,
// source lock, config lock or NGX call is acquired while holding it.
SRWLOCK energy_gate_lock=SRWLOCK_INIT;
nr_energy_gate::Streams energy_gate_streams{};
std::atomic<bool> energy_gate_loaded{false},energy_gate_any_active{false};
std::atomic<bool> energy_gate_has_evidence{false};
std::atomic<bool> energy_gate_conservative{true},energy_gate_heuristic{false};
std::atomic<bool> energy_gate_persistence_failed{false};
std::atomic<unsigned long long> energy_gate_resets{0},energy_gate_blocked_checks{0};
nr_energy_gate::Options EnergyGateOptions() {
    return {energy_gate_conservative.load(std::memory_order_acquire),
            energy_gate_heuristic.load(std::memory_order_acquire)};
}
void EnsureEnergyGateLoaded() {
    if(energy_gate_loaded.load(std::memory_order_acquire)||!g_ini_path[0])return;
    AcquireSRWLockExclusive(&energy_gate_lock);
    if(!energy_gate_loaded.load(std::memory_order_relaxed)) {
        energy_gate_conservative.store(GetPrivateProfileIntW(L"NRBeforeSR",L"PauseNrOnFullscreenUi",1,g_ini_path)!=0);
        energy_gate_heuristic.store(GetPrivateProfileIntW(L"NRBeforeSR",L"GuessFullscreenUiPause",0,g_ini_path)!=0);
        energy_gate_loaded.store(true,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&energy_gate_lock);
}
void EnergyGateTransitionLocked(nr_energy_gate::Transition transition) {
    using nr_energy_gate::Transition;
    if(transition==Transition::Resumed||transition==Transition::Stale||transition==Transition::Disabled) {
        // Publish reset BEFORE the entry and the fast-path flag can reopen NR.
        g_history_generation.fetch_add(1,std::memory_order_acq_rel);
        energy_gate_resets.fetch_add(1,std::memory_order_relaxed);
    }
}
void PublishEnergyGateLocked() {
    bool evidence=false;
    for(const auto& e:energy_gate_streams.entries)
        evidence|=e.state.Active()||e.state.explicit_samples||e.state.guess_samples;
    energy_gate_has_evidence.store(evidence,std::memory_order_release);
    energy_gate_any_active.store(energy_gate_streams.AnyActive(),std::memory_order_release);
}
void ObserveEnergyPause(nr_energy_gate::StreamKey key,unsigned long long observer,
    unsigned long long epoch,bool known,bool paused,unsigned long long frame) {
    if(!key)return;
    EnsureEnergyGateLoaded();
    if(!energy_gate_loaded.load(std::memory_order_acquire))return;
    const auto options=EnergyGateOptions();
    if(!energy_gate_has_evidence.load(std::memory_order_acquire)&&
       !(known&&paused&&options.conservative)&&!(!known&&frame&&options.heuristic))return;
    AcquireSRWLockExclusive(&energy_gate_lock);
    const auto now=GetTickCount64();
    energy_gate_streams.PollAll(options,now,EnergyGateTransitionLocked);
    energy_gate_streams.ObserveStream(key,observer,epoch,options,known,paused,frame,now,EnergyGateTransitionLocked);
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
}
bool EnergyGateBlocks(nr_energy_gate::StreamKey key) {
    // No evidence: no clock read, config I/O, lock, allocation or scanning.
    if(!key||!energy_gate_any_active.load(std::memory_order_acquire))return false;
    AcquireSRWLockExclusive(&energy_gate_lock);
    const auto now=GetTickCount64();const auto options=EnergyGateOptions();
    energy_gate_streams.PollAll(options,now,EnergyGateTransitionLocked);
    const bool blocked=energy_gate_streams.Blocked(key,options,now,EnergyGateTransitionLocked);
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
    if(blocked)energy_gate_blocked_checks.fetch_add(1,std::memory_order_relaxed);
    return blocked;
}
void RetireEnergyObserver(unsigned long long observer,unsigned long long epoch) {
    if(!energy_gate_loaded.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&energy_gate_lock);
    energy_gate_streams.Retire(observer,epoch,EnergyGateTransitionLocked);
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
}
void RetireEnergyStream(unsigned long long stream,unsigned long long epoch) {
    if(!energy_gate_has_evidence.load(std::memory_order_acquire))return;
    AcquireSRWLockExclusive(&energy_gate_lock);
    for(auto& entry:energy_gate_streams.entries)if(entry.key.stream==stream&&entry.key.epoch==epoch) {
        if(entry.state.Active())EnergyGateTransitionLocked(nr_energy_gate::Transition::Resumed);
        entry={};
    }
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
}
bool SetEnergyGateOption(bool conservative,bool enabled) {
    EnsureEnergyGateLoaded();
    AcquireSRWLockExclusive(&energy_gate_lock);
    // A menu edit made before an INI path exists still owns this session.
    energy_gate_loaded.store(true,std::memory_order_release);
    if(conservative)energy_gate_conservative.store(enabled,std::memory_order_release);
    else energy_gate_heuristic.store(enabled,std::memory_order_release);
    energy_gate_streams.PollAll(EnergyGateOptions(),GetTickCount64(),EnergyGateTransitionLocked);
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
    const bool persisted=g_ini_path[0]&&WritePrivateProfileStringW(L"NRBeforeSR",
        conservative?L"PauseNrOnFullscreenUi":L"GuessFullscreenUiPause",enabled?L"1":L"0",g_ini_path)!=FALSE;
    energy_gate_persistence_failed.store(!persisted,std::memory_order_release);
    if(!persisted)Log("[energy-gate] option applies this session; INI save failed.");
    return persisted;
}
struct EnergyGateStatus {
    bool conservative=true,heuristic=false,active=false,persistence_failed=false;
    nr_energy_gate::Cause cause=nr_energy_gate::Cause::None;
    unsigned guess_samples=0,active_streams=0;
    unsigned long long resets=0,blocked_checks=0;
};
EnergyGateStatus ReadEnergyGateStatus() {
    EnsureEnergyGateLoaded();
    EnergyGateStatus out{};const auto options=EnergyGateOptions();
    out.conservative=options.conservative;out.heuristic=options.heuristic;
    AcquireSRWLockExclusive(&energy_gate_lock);
    energy_gate_streams.PollAll(options,GetTickCount64(),EnergyGateTransitionLocked);
    for(const auto& e:energy_gate_streams.entries) {
        if(e.state.Active()){++out.active_streams;out.active=true;out.cause=e.state.ActiveCause();}
        out.guess_samples=std::max(out.guess_samples,e.state.guess_samples);
    }
    PublishEnergyGateLocked();
    ReleaseSRWLockExclusive(&energy_gate_lock);
    out.persistence_failed=energy_gate_persistence_failed.load(std::memory_order_acquire);
    out.resets=energy_gate_resets.load();out.blocked_checks=energy_gate_blocked_checks.load();
    return out;
}
}

// END ENERGY GATE PLATFORM ADAPTER
// Include after nr_post_boundary_native.inl and guide/NGX definitions, before
// ProcessEvaluate. nr_source_frame.hpp/nr_fg_contract.hpp go outside namespace.
#include "nr_late_present_receipt.hpp"
namespace nr_source_runtime {
bool CurrentEnergyPaused();
using nr_source_frame::Id;
using nr_source_frame::Source;
using nr_source_frame::Role;
using nr_source_frame::Label;
using nr_source_frame::Point;
using nr_source_frame::Ticket;
using nr_source_frame::Recording;
using nr_source_frame::Resource;
using nr_source_frame::PresentDecision;
using nr_source_frame::PresentObservation;
SRWLOCK source_lock=SRWLOCK_INIT;
nr_source_frame::Claims claims;
nr_source_frame::Lineage lineage;
std::atomic<Id> source_objects{1};
// Private cookies describe actual COM lifetimes, never pointer generations.
const GUID source_identity_guid={0x68b5d447,0x109e,0x4a98,{0xa5,0xb7,0x12,0x6e,0xec,0x43,0x8a,0x51}};
struct Lock {Lock(){AcquireSRWLockExclusive(&source_lock);}~Lock(){ReleaseSRWLockExclusive(&source_lock);}};
PresentObservation present_observation{};
Id present_observation_serial=0;
void PublishPresentObservationLocked(PresentObservation value){
    if(present_observation_serial!=UINT64_MAX)++present_observation_serial;
    value.ordinal=present_observation_serial;present_observation=value;
}
void PublishPresentObservation(PresentObservation value){Lock lock;PublishPresentObservationLocked(value);}
bool TryPresentObservation(PresentObservation& out){
    out={};
    if(!TryAcquireSRWLockShared(&source_lock))return false;
    const bool ready=present_observation.ordinal!=0;if(ready)out=present_observation;
    ReleaseSRWLockShared(&source_lock);return ready;
}
PresentDecision PresentDecisionFor(const PresentObservation& value){
    if(value.label.role==Role::Generated)return PresentDecision::GeneratedLabel;
    if(value.reject_mask&nr_source_frame::PresentRejectFgConsumed)return PresentDecision::FgConsumed;
    if(value.reject_mask&nr_source_frame::PresentRejectProcessed)return PresentDecision::InlineProcessed;
    switch(static_cast<nr_late_present::Decision>(value.receipt_decision)){
    case nr_late_present::Decision::NativeResultPending:return PresentDecision::ReceiptPending;
    case nr_late_present::Decision::NativeFailed:return PresentDecision::NativeFailed;
    case nr_late_present::Decision::WrongDevice:return PresentDecision::ReceiptDeviceMismatch;
    case nr_late_present::Decision::WrongExtent:return PresentDecision::ReceiptExtentMismatch;
    case nr_late_present::Decision::WrongConfig:return PresentDecision::ReceiptConfigMismatch;
    case nr_late_present::Decision::Expired:return PresentDecision::ReceiptExpired;
    case nr_late_present::Decision::AlreadyConsumed:return PresentDecision::ReceiptConsumed;
    case nr_late_present::Decision::Ambiguous:return PresentDecision::ReceiptAmbiguous;
    case nr_late_present::Decision::KnownGenerated:return PresentDecision::GeneratedLabel;
    case nr_late_present::Decision::KnownFgConsumed:return PresentDecision::FgConsumed;
    case nr_late_present::Decision::KnownInlineProcessed:return PresentDecision::InlineProcessed;
    case nr_late_present::Decision::Unsubmitted:return PresentDecision::Unsubmitted;
    case nr_late_present::Decision::None:
    case nr_late_present::Decision::NoReceipt:
    case nr_late_present::Decision::Accepted:return PresentDecision::ReceiptUnavailable;
    }
    return PresentDecision::Accepted;
}
Id ObjectId(ID3D12Object* object) {
    if(!object)return 0;
    Id value=0;UINT size=sizeof(value);
    if(SUCCEEDED(ID3D12Object_GetPrivateData(object,source_identity_guid,&size,&value))&&size==sizeof(value)&&value)return value;
    // Caller serializes cold cookie installation under source_lock.
    value=source_objects.fetch_add(1,std::memory_order_relaxed);
    if(!value||value==UINT64_MAX)return 0;
    return SUCCEEDED(ID3D12Object_SetPrivateData(object,source_identity_guid,sizeof(value),&value))?value:0;
}
Id DeviceOf(ID3D12DeviceChild* object) {
    if(!object)return 0;ID3D12Device* device=nullptr;
    if(FAILED(ID3D12DeviceChild_GetDevice(object,IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return 0;
    const auto id=ObjectId(reinterpret_cast<ID3D12Object*>(device));ID3D12Device_Release(device);return id;
}
Id QueueOf(ID3D12CommandQueue* queue){return ObjectId(reinterpret_cast<ID3D12Object*>(queue));}
#include "nr_source_observer.inl"
struct CoreRecordingDevice {Id list=0,lifetime=0,device=0;};
CoreRecordingDevice core_recording_devices[256]{};
unsigned next_core_recording_device=0;
Id CoreRecordingDeviceOfLocked(const nr_post_boundary::List& core) {
    for(const auto& entry:core_recording_devices)
        if(entry.list==core.address&&entry.lifetime==core.token)return entry.device;
    return 0;
}
void RememberCoreRecordingDeviceLocked(const nr_post_boundary::List& core,Id device) {
    if(!core.known||!core.address||!core.token||!device)return;
    for(auto& entry:core_recording_devices)if(entry.list==core.address&&entry.lifetime==core.token){entry.device=device;return;}
    core_recording_devices[next_core_recording_device]={core.address,core.token,device};
    next_core_recording_device=(next_core_recording_device+1)%std::size(core_recording_devices);
}
// Source Evaluate construction is a cold boundary. Resolve the real device
// cookie here so recording hooks and Execute only read bounded value records.
void SeedCoreRecordingDevice(ID3D12GraphicsCommandList* list) {
    if(!list)return;Lock lock;
    const auto core=SnapshotPostBoundaryList(list);
    if(!core.known||core.reset==UINT64_MAX)return;
    RememberCoreRecordingDeviceLocked(core,DeviceOf(reinterpret_cast<ID3D12DeviceChild*>(list)));
}
Recording RecordingOf(ID3D12GraphicsCommandList* list) {
    // Prefer the canonical Core receipt whenever it exists. That keeps a
    // validated proxy alias and its native Execute in one generation domain,
    // even if the separate source observer becomes ready between the two.
    const auto owned=ObservedRecording(list);
    const auto core=SnapshotPostBoundaryList(list);
    if(core.known) {
        // Core counts the first open recording as reset 0; +1 reserves zero as unknown.
        if(core.reset==UINT64_MAX)return {};
        Id device=owned.device;
        // Entries cache their device cookie before hook publication. This also
        // covers ready=false without calling COM from a native command hook.
        for(unsigned i=0;i<256;++i)if(observed_lists[i].native==list ||
                reinterpret_cast<Id>(observed_lists[i].native)==core.address) {
            if(!device)device=observed_lists[i].device;break;
        }
        if(!device)device=CoreRecordingDeviceOfLocked(core);
        if(!device)return {};
        RememberCoreRecordingDeviceLocked(core,device);
        return {core.address,core.token,core.reset+1,device};
    }
    // A refused/installing source observer is not an identity veto: the Core
    // lookup above still had its chance before this independent-domain fallback.
    return owned;
}
Resource ResourceOf(ID3D12Resource* resource) {
    if(!resource)return {};
    return {reinterpret_cast<Id>(resource),ObjectId(reinterpret_cast<ID3D12Object*>(resource)),
        DeviceOf(reinterpret_cast<ID3D12DeviceChild*>(resource)),0};
}
struct Rect {unsigned x=0,y=0,width=0,height=0;};
struct NativeInputs {
    Source source{};
    ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr,*hudless=nullptr,*ui=nullptr,*ui_alpha=nullptr,*distortion=nullptr;
    Rect color_rect{},depth_rect{},motion_rect{},hudless_rect{};
    float motion_scale_x=0,motion_scale_y=0,jitter_x=0,jitter_y=0;
    bool motion_scale_known=false,jitter_known=false,depth_inverted=false,hdr=false,reset=true,paused=false;
    bool paused_known=false;
    Id fg_frame=0; // The same public frame read serves lineage and pause evidence.
    bool fg=false,states_known=false;
    // FG jitter is clip space and is deliberately not mislabeled source pixels.
    bool jitter_clip_space=false;
    unsigned color_state=0,depth_state=0,motion_state=0;
    unsigned hudless_state=0,ui_state=0,ui_alpha_state=0;
    bool hudless_state_known=false,ui_state_known=false,ui_alpha_state_known=false;
    unsigned encoding=0;
};
struct PresentLease {
    Source source{};Id source_id=0;
    Role role=Role::Unknown;
    NativeInputs inputs{};
    NrGuideBinding guides{};
    NrGuideSignature source_signature{};
    bool guides_owned=false,guides_current=false;
    bool color_only=false; // Exact submitted color lineage; no native guide claim.
    bool application_source=false; // Independent full-write producer, not a native result.
    Id application_chain=0,application_epoch=0;
    unsigned application_config=0;
    Id color_submission=0,color_content=0;
    bool source_jitter_known=false,output_jitter_known=false,cross_queue_wait=false;
    Id queue=0,producer_queue=0,producer_value=0;
    ID3D12Fence* producer_fence=nullptr;
    unsigned source_age=0,output_width=0,output_height=0;
};
thread_local PresentLease present_lease{};
thread_local Ticket present_ticket{};
thread_local bool present_recorded=false;
thread_local Resource present_resource{};
struct ResourceState {Recording recording{};Resource resource{};unsigned state=0;};
ResourceState resource_states[256]{};
bool StateOf(Recording recording,Resource resource,unsigned& state) {
    for(const auto& entry:resource_states)if(entry.recording==recording&&entry.resource==resource){state=entry.state;return true;}
    return false;
}
void OnBarrier(ID3D12GraphicsCommandList* list,unsigned count,const D3D12_RESOURCE_BARRIER* barriers) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)||!list||!barriers||count>256)return;
    Lock lock;const auto recording=RecordingOf(list);if(!recording)return;
    for(unsigned i=0;i<count;++i){const auto& b=barriers[i];
        if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){lineage.InvalidateDevice(recording);source_receipts_pending.store(true,std::memory_order_release);
            for(auto& entry:resource_states)if(entry.recording==recording)entry={};
            continue;}
        if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)continue;
        const auto resource=ResourceOf(b.Transition.pResource);if(!resource)continue;
        if(b.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE||b.Transition.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){
            for(auto& entry:resource_states)if(entry.recording==recording&&entry.resource==resource)entry={};
            continue;}
        ResourceState* available=nullptr;
        for(auto& entry:resource_states){if(entry.recording==recording&&entry.resource==resource){available=&entry;break;}
            if(!entry.recording&&!available)available=&entry;}
        if(available)*available={recording,resource,static_cast<unsigned>(b.Transition.StateAfter)};
    }
}
void OnEnhancedBarrier(ID3D12GraphicsCommandList* list){
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)||!list)return;
    Lock lock;const auto recording=RecordingOf(list);if(!recording)return;
    // Enhanced layout/access/sync is not interchangeable with legacy state
    // bits. A previously observed legacy state cannot survive this boundary.
    for(auto& entry:resource_states)if(entry.recording==recording)entry={};
    lineage.InvalidateDevice(recording);
    source_receipts_pending.store(true,std::memory_order_release);
}
struct Producer {Id handle=0,epoch=0,next=0,stream=0,device=0;bool native=false;};
Producer producers[64]{};
void ObserveNativeProducer(Id handle,Id epoch,Id device){
    Producer* available=nullptr;
    for(auto& producer:producers){if(producer.handle==handle&&producer.epoch==epoch){available=&producer;break;}
        if(!producer.handle&&!available)available=&producer;}
    if(!available||!device)return;
    if(!available->handle){const auto stream=source_objects.fetch_add(1,std::memory_order_relaxed);if(!stream||stream==UINT64_MAX)return;
        *available={handle,epoch,0,stream};}
    available->device=device;available->native=true;
}
bool NativeProducerOnDevice(Id device){for(const auto& producer:producers)if(producer.native&&producer.device==device)return true;return false;}
struct RetryIdentity{Source source{};Id token=0;};
RetryIdentity retry_identities[256]{};
Id next_retry_token=1;
Id RetryToken(Source source){
    if(!source)return 0;
    for(const auto& item:retry_identities)if(item.source==source)return item.token;
    for(const auto& item:retry_identities)if(item.source.stream==source.stream&&item.source.epoch==source.epoch&&item.source.sequence>source.sequence)return 0;
    if(next_retry_token==UINT64_MAX)return 0;
    // Capacity is reclaimed by actual producer retirement. An unremembered
    // older source never manufactures another retry attempt after eviction.
    for(auto& item:retry_identities)if(!item.source){item={source,next_retry_token++};return item.token;}
    for(auto& item:retry_identities)if(item.source.stream==source.stream&&item.source.epoch==source.epoch&&item.source.sequence<source.sequence){
        item={source,next_retry_token++};return item.token;}
    return 0;
}
bool ProducerActive(Source source){for(const auto& p:producers)if(p.stream==source.stream&&p.epoch==source.epoch)return true;return false;}
void RetireReceiptStreamLocked(Id stream,Id epoch);
void RetireProducer(Id handle,Id epoch=0){
    nr_processing_runtime::RetireEnergyObserver(handle,epoch);
    for(auto& p:producers)if(p.handle==handle&&(!epoch||p.epoch==epoch)){
        nr_processing_runtime::RetireEnergyStream(p.stream,p.epoch);
        for(auto& retry:retry_identities)if(retry.source.stream==p.stream&&retry.source.epoch==p.epoch)retry={};
        lineage.RetireSource(p.stream,p.epoch);claims.RetireStream(p.stream,p.epoch);
        RetireReceiptStreamLocked(p.stream,p.epoch);p={};}
}
void RetireFeature(const NgxHandle* handle,unsigned long long generation){Lock lock;RetireProducer(reinterpret_cast<Id>(handle),generation);}
struct ApplicationSurface {
    Id chain=0,owner=0,device=0,epoch=0,latest_content=0,content_floor=0;
    std::array<Resource,16> buffers{};
    unsigned count=0;
};
ApplicationSurface application_surfaces[16]{};
void RetireApplicationSurface(Id chain) {
    if(!chain)return;
    for(auto& surface:application_surfaces)if(surface.chain==chain){
        RetireProducer(chain,surface.epoch);surface={};}
}
void RetireApplicationResource(Id address) {
    for(auto& surface:application_surfaces)if(surface.chain)
        for(unsigned i=0;i<surface.count;++i)if(surface.buffers[i].address==address){
            RetireApplicationSurface(surface.chain);break;}
}
void RetireApplicationDevice(Id owner) {
    if(!owner)return;
    for(auto& surface:application_surfaces)if(surface.owner==owner)RetireApplicationSurface(surface.chain);
}
void RegisterApplicationSurface(Id chain,Id owner,const std::array<Resource,16>& buffers,unsigned count) {
    RetireApplicationSurface(chain);
    if(!chain||!owner||!count||count>buffers.size()||!buffers[0])return;
    for(unsigned i=0;i<count;++i)if(!buffers[i]||buffers[i].device!=buffers[0].device)return;
    for(auto& surface:application_surfaces)if(!surface.chain){
        const auto epoch=source_objects.fetch_add(1,std::memory_order_relaxed);
        if(!epoch||epoch==UINT64_MAX)return;
        surface={chain,owner,buffers[0].device,epoch,0,lineage.ContentEpoch(),buffers,count};return;
    }
}
ApplicationSurface* ApplicationSurfaceFor(Id chain,Resource resource) {
    for(auto& surface:application_surfaces)if(surface.chain==chain&&surface.device==resource.device)
        for(unsigned i=0;i<surface.count;++i)if(surface.buffers[i]==resource)return &surface;
    return nullptr;
}
void ForgetPresentDemand(Id chain);
void RetireSwapchain(reshade::api::swapchain* chain){Lock lock;const auto id=reinterpret_cast<Id>(chain);
    ForgetPresentDemand(id);RetireApplicationSurface(id);RetireProducer(id);}
Source ProducerSource(Id handle,Id epoch,Id supplied=0) {
    if(!handle||!epoch)return {};
    Producer* free=nullptr;
    for(auto& p:producers){if(p.handle==handle&&p.epoch==epoch){
        if(!supplied && p.next==UINT64_MAX)return {};
        const Id sequence=supplied?supplied:++p.next;return {p.stream,epoch,sequence};}
        if(!p.handle&&!free)free=&p;}
    if(!free)return {};
    const auto stream=source_objects.fetch_add(1,std::memory_order_relaxed);
    if(!stream||stream==UINT64_MAX)return {};
    *free={handle,epoch,supplied?supplied:1,stream};return {stream,epoch,supplied?supplied:1};
}
// Streamline adapter publishes the API's public frame token value, not its
// pointer and not a newly fabricated slGetNewFrameToken call.
thread_local Source sl_source{},sl_present_source{};
Source PublishStreamlineSource(Id provider,Id epoch,unsigned viewport,unsigned frame) {
    Lock lock;
    const Id view=(Id(viewport)<<32)^provider;
    return ProducerSource(view?view:provider,epoch,Id(frame)+1);
}
struct StreamlineScope {
    Source previous{};
    explicit StreamlineScope(Source value):previous(sl_source){sl_source=value;}
    ~StreamlineScope(){sl_source=previous;}
};
bool ReadFgResource(const NgxParams* params,const char* key,ID3D12Resource*& resource) {
    resource=nullptr;
    const auto typed=ParamSlot<GetD3D12Fn>(params,9);
    if(typed&&typed(params,key,&resource)==kNgxSuccess&&resource)return true;
    resource=nullptr;void* value=nullptr;const auto generic=ParamSlot<GetVoidFn>(params,8);
    // Called only with public FG resource-key constants, never arbitrary keys.
    if(!generic||generic(params,key,&value)!=kNgxSuccess||!value)return false;
    resource=static_cast<ID3D12Resource*>(value);return true;
}
bool ReadFrame(const NgxParams* params,Id& value) {
    using Fn=NgxResult(*)(const NgxParams*,const char*,unsigned long long*);
    const auto get=ParamSlot<Fn>(params,15);unsigned long long result=0;
    if(!get||get(params,nr_fg_contract::frame,&result)!=kNgxSuccess)return false;
    // NGX permits frame zero. Reserve internal sequence zero for unknown,
    // without wrapping the public uint64 frame counter into another source.
    if(result==UINT64_MAX)return false;
    value=result+1;return true;
}
Rect ReadRect(const NgxParams* params,ID3D12Resource* resource,const char* prefix) {
    if(!resource)return {};const auto d=ID3D12Resource_GetDesc(resource);
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width>UINT_MAX||d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1)return {};
    Rect r{0,0,static_cast<unsigned>(d.Width),d.Height};char key[96]{};
    auto read=[&](const char* suffix,unsigned& value){std::snprintf(key,sizeof(key),"%s%s",prefix,suffix);unsigned v=0;if(GetUInt(params,key,&v))value=v;};
    read("BaseX",r.x);read("BaseY",r.y);read("Width",r.width);read("Height",r.height);
    if(!r.width||!r.height||r.x>=d.Width||r.y>=d.Height||r.width>d.Width-r.x||r.height>d.Height-r.y)return {};
    return r;
}
NativeInputs ReadFgInputs(const NgxParams* params) {
    NativeInputs input{};input.fg=true;input.jitter_clip_space=true;
    ReadFgResource(params,nr_fg_contract::backbuffer,input.color);ReadFgResource(params,nr_fg_contract::depth,input.depth);
    ReadFgResource(params,nr_fg_contract::motion,input.motion);ReadFgResource(params,nr_fg_contract::hudless,input.hudless);
    ReadFgResource(params,nr_fg_contract::ui,input.ui);ReadFgResource(params,nr_fg_contract::ui_alpha,input.ui_alpha);
    ReadFgResource(params,nr_fg_contract::distortion,input.distortion);
    input.color_rect=ReadRect(params,input.color,"DLSSG.InputBackbufferSubrect");
    input.depth_rect=ReadRect(params,input.depth,"DLSSG.DepthSubrect");
    input.motion_rect=ReadRect(params,input.motion,"DLSSG.MVecsSubrect");
    input.hudless_rect=ReadRect(params,input.hudless,"DLSSG.HUDLessSubrect");
    input.motion_scale_known=GetFloat(params,nr_fg_contract::mv_x,&input.motion_scale_x)&&
        GetFloat(params,nr_fg_contract::mv_y,&input.motion_scale_y)&&std::isfinite(input.motion_scale_x)&&std::isfinite(input.motion_scale_y);
    input.jitter_known=GetFloat(params,nr_fg_contract::jitter_x,&input.jitter_x)&&
        GetFloat(params,nr_fg_contract::jitter_y,&input.jitter_y)&&std::isfinite(input.jitter_x)&&std::isfinite(input.jitter_y);
    unsigned v=0;if(GetUInt(params,nr_fg_contract::reset,&v))input.reset=v!=0;
    v=0;input.paused_known=GetUInt(params,nr_fg_contract::paused,&v);
    if(input.paused_known)input.paused=v!=0;
    (void)ReadFrame(params,input.fg_frame);
    v=0;if(GetUInt(params,nr_fg_contract::inverted,&v))input.depth_inverted=v!=0;
    v=0;if(GetUInt(params,nr_fg_contract::hdr,&v))input.hdr=v!=0;
    return input;
}
bool ApplicationPresentCurrent(const PresentLease& lease,const Label& label,Resource resource,Id queue,unsigned config) {
    const auto* surface=ApplicationSurfaceFor(lease.application_chain,resource);
    return lease.application_source&&surface&&surface->epoch==lease.application_epoch&&
        lease.application_config==config&&surface->latest_content==label.content&&
        label.content==lease.color_content&&label.submission==lease.color_submission&&
        queue==lease.queue&&nr_source_frame::PresentPromotionEligible(label,queue)&&!label.source&&
        !NativeProducerOnDevice(resource.device)&&!g_fg_presentation_policy.Read().late_owned&&
        ProducerActive(lease.source)&&!claims.WasRecorded(lease.source);
}
bool AcquireApplicationPresentColor(Id chain,const Label& label,Resource resource,unsigned width,unsigned height,Id queue,unsigned config) {
    if(!width||!height||!nr_source_frame::PresentPromotionEligible(label,queue)||label.source||
        NativeProducerOnDevice(resource.device)||g_fg_presentation_policy.Read().late_owned)return false;
    auto* surface=ApplicationSurfaceFor(chain,resource);
    if(!surface||label.content<=surface->content_floor||label.content<surface->latest_content)return false;
    // Content is allocated by actual ordered application writes, never Present.
    // Exact copies retain it; duplicate flips cannot manufacture a new sequence.
    const auto source=ProducerSource(chain,surface->epoch,label.content);
    if(!source||!claims.Available(source))return false;
    surface->latest_content=label.content;
    present_lease={};present_lease.source=source;present_lease.source_id=label.content;
    present_lease.role=Role::Application;present_lease.color_only=true;present_lease.application_source=true;
    present_lease.application_chain=chain;present_lease.application_epoch=surface->epoch;present_lease.application_config=config;
    present_lease.color_submission=label.submission;present_lease.color_content=label.content;
    present_lease.queue=present_lease.producer_queue=queue;
    present_lease.output_width=width;present_lease.output_height=height;
    return ApplicationPresentCurrent(present_lease,label,resource,queue,config);
}
#include "nr_source_transport.inl"
void ForgetPresentDemand(Id chain){present_demands.Forget(chain);}
void BeginNativeReceiptOpportunity(Source source) {
    Lock lock;Id device=0;
    for(const auto& producer:producers)if(producer.stream==source.stream&&producer.epoch==source.epoch){device=producer.device;break;}
    BeginNativeReceiptOpportunityLocked(source,device);
}
bool CaptureGuides(ID3D12GraphicsCommandList* list,Source source,const NrGuideBinding& input,
    unsigned output_width,unsigned output_height) {
    return CaptureGuides(list,source,input,output_width,output_height,g_config_generation.load(std::memory_order_relaxed));
}
bool SourceGuidesAwaitSubmission(){return guides_awaiting_submission.load(std::memory_order_acquire);}
bool HasPendingSubmission(reshade::api::command_list* owner){
    if(!MayObserveSubmission()||!owner||!owner->get_device()||owner->get_device()->get_api()!=reshade::api::device_api::d3d12)return false;
    auto* native=reinterpret_cast<ID3D12GraphicsCommandList*>(owner->get_native());Lock lock;const auto recording=RecordingOf(native);
    if(!recording)return false;
    if(lineage.PendingList(recording))return true;
    for(const auto& copy:guide_copies)if(copy.recorded&&!copy.submitted&&!copy.poisoned&&copy.recording==recording)return true;
    return false;
}
void ResolveInputStates(ID3D12GraphicsCommandList* list,NativeInputs& inputs) {
    Lock lock;const auto recording=RecordingOf(list);
    inputs.states_known=StateOf(recording,ResourceOf(inputs.color),inputs.color_state)&&
        StateOf(recording,ResourceOf(inputs.depth),inputs.depth_state)&&StateOf(recording,ResourceOf(inputs.motion),inputs.motion_state);
    inputs.hudless_state_known=inputs.hudless&&StateOf(recording,ResourceOf(inputs.hudless),inputs.hudless_state);
    inputs.ui_state_known=inputs.ui&&StateOf(recording,ResourceOf(inputs.ui),inputs.ui_state);
    inputs.ui_alpha_state_known=inputs.ui_alpha&&StateOf(recording,ResourceOf(inputs.ui_alpha),inputs.ui_alpha_state);
}
NrGuideBinding FgGuides(const NativeInputs& input) {
    NrGuideBinding g{};if(!input.depth||!input.motion||!input.color||!input.motion_scale_known||
        !input.depth_rect.width||!input.motion_rect.width||!input.color_rect.width)return g;
    g.depth=input.depth;g.motion=input.motion;g.game_reset=input.reset?1:0;
    auto& s=g.signature;const auto d=ID3D12Resource_GetDesc(g.depth),m=ID3D12Resource_GetDesc(g.motion),c=ID3D12Resource_GetDesc(input.color);
    s.depth_physical_width=d.Width;s.depth_physical_height=d.Height;s.depth_format=static_cast<unsigned>(d.Format);
    s.motion_physical_width=m.Width;s.motion_physical_height=m.Height;s.motion_format=static_cast<unsigned>(m.Format);
    s.color_physical_width=c.Width;s.color_physical_height=c.Height;s.color_format=static_cast<unsigned>(c.Format);
    s.depth_x=input.depth_rect.x;s.depth_y=input.depth_rect.y;s.depth_width=input.depth_rect.width;s.depth_height=input.depth_rect.height;
    s.motion_x=input.motion_rect.x;s.motion_y=input.motion_rect.y;s.motion_source_width=input.motion_rect.width;s.motion_source_height=input.motion_rect.height;
    s.motion_bound_width=s.motion_source_width;s.motion_bound_height=s.motion_source_height;
    s.color_x=input.color_rect.x;s.color_y=input.color_rect.y;
    // Official FG scales normalize to [-1,1]; NR scales describe raster pixels.
    s.motion_scale_x=input.motion_scale_x*input.color_rect.width;s.motion_scale_y=input.motion_scale_y*input.color_rect.height;
    g.has_motion_scale_x=g.has_motion_scale_y=g.valid_motion_scale_x=g.valid_motion_scale_y=true;
    s.depth_inverted=input.depth_inverted?1:0;s.normalized_create_flags=(input.hdr?1:0)|(input.depth_inverted?8:0);
    if(s.motion_source_width!=input.color_rect.width||s.motion_source_height!=input.color_rect.height)s.normalized_create_flags|=2;
    g.jitter.known=false;return g;
}
void CaptureNgxGuides(ID3D12GraphicsCommandList* list,const NgxParams* params,const FeatureRecord& feature,Source source) {
    if(!source||!params||!nr_processing_runtime::PresentSelected())return;
    GuideCaptureAttempt capture_diagnostic(source);GuideCaptureStage("configuration-disabled");
    // Scalar configuration is snapshotted under the same Core lock as other
    // native guide binding; command copies execute after releasing that lock.
    struct ConfigLock{bool held=true;ConfigLock(){EnterCriticalSection(&g_chain_cs);}void Release(){if(held){LeaveCriticalSection(&g_chain_cs);held=false;}}~ConfigLock(){Release();}} config_lock;
    if(!g_config.enabled||g_config.identity_use_original||!std::isfinite(g_config.transfer_strength)||g_config.transfer_strength<=0.000001f)return;
    GuideCaptureStage("runtime-or-owner-blocked");
    if(g_nr_failed_create_process_latched.load(std::memory_order_acquire)||g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)||
       OtherNrOwnerActive()||g_codec.failed)return;
    AcquireSRWLockShared(&g_nr_api_lock);const bool api_failed=g_nr_api.failed;ReleaseSRWLockShared(&g_nr_api_lock);
    if(api_failed)return;
    const unsigned config_generation=g_config_generation.load(std::memory_order_relaxed);
    Id retry=0;{Lock lock;retry=RetryToken(source);}
    // Resolve a provisional fallback before paying for its guide transport.
    // Otherwise a skipped previous frame could trigger Present copies and then
    // return to inline NR later in CarrierScope for this same new source.
    GuideCaptureStage("route-not-ready");
    nr_processing_runtime::BeginSource(retry);
    if(!nr_processing_runtime::session.Ready(nr_processing::Start::Present,retry))return;
    GuideCaptureStage("native-resource-or-output-extent");
    ID3D12Resource* color=nullptr;ID3D12Resource* output=nullptr;
    if(!GetResource(params,"Color",&color)||!GetResource(params,"Output",&output))return;
    unsigned x=0,y=0,ox=0,oy=0,rw=0,rh=0,ow=0,oh=0;
    GetUInt(params,"DLSS.Input.Color.Subrect.Base.X",&x);GetUInt(params,"DLSS.Input.Color.Subrect.Base.Y",&y);
    GetUInt(params,"DLSS.Output.Subrect.Base.X",&ox);GetUInt(params,"DLSS.Output.Subrect.Base.Y",&oy);
    if(!ResolveRenderExtentFromEvaluate(params,feature,color,x,y,rw,rh)||
       !ResolveOutputExtentFromEvaluate(params,feature,output,ox,oy,rw,rh,ow,oh))return;
    // ResolveOutputExtent already selected a bounded logical viewport. We do
    // not read/copy Output here, so a nonzero Output atlas origin alone is not
    // a reason to reject its independently bounded Depth/MV active regions.
    if(!PresentDemandNeedsCapture(source,config_generation,ow,oh)) {
        GuideCaptureStage("consumer-known-extents-incompatible-before-snapshot");return;
    }
    NrGuideBinding guides{};
    GuideCaptureStage("native-guide-binding");
    const bool resolved=ResolveNrGuideBinding(params,feature,1,ow,oh,rw,rh,color,x,y,CodecColorMode::Automatic,guides)&&
       ResolveRrLinearDepth(params,feature,guides);
    config_lock.Release();if(resolved)(void)CaptureGuides(list,source,guides,ow,oh,config_generation,true);
}
// Callback executes at the last actual FG input consumer, BEFORE the original
// FG call. It must preserve native state and update both scene/HUDless and final
// color coherently; false means no recorded work, not an opaque failure.
using FgConsumer=bool(*)(ID3D12GraphicsCommandList*,const NativeInputs&);
FgConsumer fg_consumer=nullptr;
void SetFgConsumer(FgConsumer consumer){Lock lock;fg_consumer=consumer;}
struct EvaluateScope;
thread_local EvaluateScope* evaluation=nullptr;
struct EvaluateScope {
    EvaluateScope* previous=nullptr;ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Resource *output=nullptr,*generated=nullptr,*real=nullptr;
    Source source{};Ticket ticket{};NativeInputs inputs{};Id retry_token=0,energy_device=0;
    int feature=0;bool active=false,finished=false,private_recorded=false;NgxResult fatal=0;
    EvaluateScope(ID3D12GraphicsCommandList* commands,const NgxHandle* handle,const NgxParams* params,int feature_id,Id generation)
        :previous(evaluation),list(commands),feature(feature_id) {
        if(previous||!commands||!handle||!params||!generation||(feature!=1&&feature!=13&&feature!=11))return;
        if(!nr_processing_runtime::work_enabled.load(std::memory_order_acquire))return;
        SeedCoreRecordingDevice(commands);
        active=true;evaluation=this;nr_processing_runtime::energy_current_query=CurrentEnergyPaused;
        {Lock lock;energy_device=RecordingOf(commands).device;
            ObserveNativeProducer(reinterpret_cast<Id>(handle),generation,energy_device);}
        FgConsumer consume=nullptr;
        if(feature==11){inputs=ReadFgInputs(params);output=inputs.color;
            ReadFgResource(params,nr_fg_contract::generated,generated);ReadFgResource(params,nr_fg_contract::real,real);
            Lock lock;const auto recording=RecordingOf(commands);
            const auto inherited=lineage.Lookup(ResourceOf(output),recording);
            inputs.encoding=inherited.encoding;
            if(inherited.KnownApplication())source=inherited.source;
            else if(sl_source)source=sl_source;else if(sl_present_source)source=sl_present_source;
            else if(inputs.fg_frame)source=ProducerSource(reinterpret_cast<Id>(handle),generation,inputs.fg_frame);
            inputs.source=source;consume=fg_consumer;
        }else{
            GetResource(params,"Output",&output);
            Lock lock;source=sl_source?sl_source:ProducerSource(reinterpret_cast<Id>(handle),generation);
            Id device=0;for(const auto& producer:producers)if(producer.handle==reinterpret_cast<Id>(handle)&&producer.epoch==generation){device=producer.device;break;}
            BeginNativeReceiptOpportunityLocked(source,device);
        }
        {Lock lock;retry_token=RetryToken(source);}
        // The FG signal can only gate the exact stream established by lineage,
        // Streamline or this Feature's producer epoch; never an unrelated view.
        if(feature==11) {
            nr_processing_runtime::ObserveEnergyPause({energy_device,source.stream,source.epoch},
                reinterpret_cast<Id>(handle),generation,inputs.paused_known,inputs.paused,inputs.fg_frame);
        }
        const bool energy_paused=nr_processing_runtime::EnergyGateBlocks({energy_device,source.stream,source.epoch});
        // No source lock is held across private/GPU or native callbacks.
        if(feature==11&&source&&!inputs.paused&&!energy_paused){ResolveInputStates(list,inputs);
            const auto guides=FgGuides(inputs);
            if(consume&&inputs.states_known&&inputs.encoding&&guides.depth&&retry_token){
                // Feature11 is the last truthful source-input boundary even
                // when no reconstruction feature exists. Reserve the source
                // before advancing the hidden suffix; another view/thread or
                // previously recorded/FG-consumed image cannot move it.
                {Lock lock;if(ProducerActive(source)&&claims.Available(source)&&!claims.WasRecorded(source))
                    ticket=claims.Begin({source,Role::Application},Point::Present);}
                if(ticket){EnterCriticalSection(&g_chain_cs);nr_processing_runtime::Reconcile();
                    if(nr_processing_runtime::work_enabled.load(std::memory_order_acquire))
                        nr_processing_runtime::MissingInlineAtPresent(retry_token);
                    LeaveCriticalSection(&g_chain_cs);}
            }
            // FG consumes these original guides on this same borrowed list.
            // Successful FG closes this source against late Present NR, so an
            // owned Present transport copy here would have no legal consumer.
            if(consume)(void)consume(list,inputs);
        }else if((feature==1||feature==13)&&!energy_paused){const auto record=FindFeature(handle);CaptureNgxGuides(list,params,record,source);}
    }
    EvaluateScope(ID3D12GraphicsCommandList* commands,const NgxHandle* handle,const NgxParams* params,const FeatureRecord& record)
        :EvaluateScope(commands,handle,params,record.feature_id,record.generation){}
    NgxResult FatalResult() const {return fatal;}
    void Finish(NgxResult result) {
        if(!active||finished)return;finished=true;Lock lock;
        const auto recording=RecordingOf(list);
        if(feature==1||feature==13)CompleteNativeReceiptLocked(source,result==kNgxSuccess);
        if(result==kNgxSuccess){
            const auto application=ProducerActive(source)?Role::Application:Role::Retired;
            Label app{source,application};app.processed=claims.WasRecorded(source);app.encoding=inputs.encoding;
            if(feature==11){app.fg_consumed=true;claims.ConsumeFg(source);MarkReceiptFgConsumedLocked(source);}
            if(output)lineage.Set(recording,ResourceOf(output),app);
            if(feature==11){
                if(real)lineage.Set(recording,ResourceOf(real),app);
                if(generated)lineage.Set(recording,ResourceOf(generated),{source,Role::Generated});
            }
        }else{
            if(output)lineage.Set(recording,ResourceOf(output),{source,Role::Retired});
            if(real)lineage.Set(recording,ResourceOf(real),{source,Role::Retired});
            if(generated)lineage.Set(recording,ResourceOf(generated),{source,Role::Generated});
        }
        if(lineage.PendingList(recording))source_receipts_pending.store(true,std::memory_order_release);
    }
    ~EvaluateScope(){if(!active)return;Lock lock;
        if(ticket){if(private_recorded)claims.RetainRecorded(ticket);else claims.RejectBeforeWork(ticket);}
        evaluation=previous;
    }
};
Id CurrentRetryToken(){return evaluation?evaluation->retry_token:0;}
Source CurrentSource(){return evaluation?evaluation->source:Source{};}
bool CurrentEnergyPaused() {
    const auto* current=evaluation;
    return current&&nr_processing_runtime::EnergyGateBlocks(
        {current->energy_device,current->source.stream,current->source.epoch});
}
void ReportFgFailure(NgxResult result){if(evaluation){Lock lock;evaluation->fatal=result?result:0xBAD00005u;
    if(evaluation->ticket)claims.Complete(evaluation->ticket,false,false);}}
bool InlineAllowed(unsigned route) {
    auto* current=evaluation;
    if(!current||!current->active||!current->source)return false;
    if(CurrentEnergyPaused())return false;
    const Point point=route==2?Point::Before:route==1?Point::After:Point::Present;
    Lock lock;
    if(current->ticket){if(current->ticket.point==point)return true;
        if(current->private_recorded)return false;
        claims.RejectBeforeWork(current->ticket);current->ticket={};}
    current->ticket=claims.Begin({current->source,Role::Application},point);return bool(current->ticket);
}
void MarkPrivateRecorded() {
    Lock lock;
    if(evaluation&&evaluation->ticket){claims.Record(evaluation->ticket);evaluation->private_recorded=true;
        MarkReceiptInlineProcessedLocked(evaluation->ticket.source);}
    else if(present_ticket){claims.Record(present_ticket);present_recorded=true;}
}
void OnExecute(ID3D12CommandQueue* queue,unsigned count,ID3D12CommandList*const* lists,bool succeeded) {
    CollectGuideInputs();
    const bool enabled=nr_processing_runtime::work_enabled.load(std::memory_order_relaxed);
    if((!enabled&&!guides_awaiting_submission.load(std::memory_order_acquire))||!queue||!lists||count>256)return;Lock lock;
    Id queue_identity=0;
    for(unsigned i=0;i<count;++i)if(lists[i]){
        auto* native=reinterpret_cast<ID3D12GraphicsCommandList*>(lists[i]);
        const auto recording=RecordingOf(native);if(!recording)continue;
        bool guides=false;for(const auto& copy:guide_copies)if(copy.recorded&&!copy.submitted&&!copy.poisoned&&
            copy.recording.list==recording.list&&copy.recording.lifetime==recording.lifetime){guides=true;break;}
        if(!guides&&(!enabled||!lineage.TracksList(recording.list,recording.lifetime)))continue;
        if(!queue_identity)queue_identity=QueueOf(queue);if(!queue_identity)continue;
        // Only a list with an actual source receipt pays for the device cookie.
        if(enabled)lineage.Submit(recording,queue_identity,succeeded);
        // Capture owns the recording identity chosen while the commands were
        // actually appended. Observer readiness can change before Execute, so
        // submit matching copies by stable list lifetime using their own token.
        for(auto& copy:guide_copies)if(copy.recorded&&!copy.submitted&&!copy.poisoned&&
            copy.recording.list==recording.list&&copy.recording.lifetime==recording.lifetime)
            GuideCopiesSubmitted(queue,copy.recording,succeeded);
    }
    source_receipts_pending.store(lineage.AnyPending()||guides_awaiting_submission.load(std::memory_order_relaxed),std::memory_order_release);
}
void OnReset(ID3D12GraphicsCommandList* list,bool succeeded) {
    if(!succeeded||!list)return;
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)&&!guides_awaiting_submission.load(std::memory_order_acquire)){
        Lock lock;const auto recording=RecordingOf(list);if(!recording)return;
        lineage.Reset(recording);for(auto& state:resource_states)if(state.recording.list==recording.list&&state.recording.lifetime==recording.lifetime)state={};
        AdvanceObservedList(list);source_receipts_pending.store(lineage.AnyPending(),std::memory_order_release);return;
    }
    GuideCopy discarded[8]{};unsigned count=0;
    {Lock lock;const auto recording=RecordingOf(list);lineage.Reset(recording);
        for(auto& state:resource_states)if(state.recording.list==recording.list&&state.recording.lifetime==recording.lifetime)state={};
        for(auto& copy:guide_copies)if(!copy.submitted&&!copy.poisoned&&!copy.reserved&&copy.recording.list==recording.list&&copy.recording.lifetime==recording.lifetime){discarded[count++]=copy;copy={};}
        AdvanceObservedList(list);source_receipts_pending.store(lineage.AnyPending()||guides_awaiting_submission.load(std::memory_order_relaxed),std::memory_order_release);}
    for(unsigned i=0;i<count;++i)ClearGuideCopy(discarded[i]);
}
void OnCopy(ID3D12GraphicsCommandList* list,ID3D12Resource* destination,ID3D12Resource* source,bool complete) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;Lock lock;
    lineage.Copy(RecordingOf(list),ResourceOf(destination),ResourceOf(source),complete);
    source_receipts_pending.store(lineage.AnyPending(),std::memory_order_release);
}
void OnWrite(ID3D12GraphicsCommandList* list,ID3D12Resource* destination) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;Lock lock;
    const auto recording=RecordingOf(list);
    if(destination)lineage.Invalidate(recording,ResourceOf(destination));else lineage.InvalidateDevice(recording);
    if(lineage.PendingList(recording))source_receipts_pending.store(true,std::memory_order_release);
}
void OnClear(ID3D12GraphicsCommandList* list,ID3D12Resource* destination,bool full) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;Lock lock;
    if(full)lineage.Clear(RecordingOf(list),ResourceOf(destination));
    else lineage.Invalidate(RecordingOf(list),ResourceOf(destination));
    source_receipts_pending.store(lineage.AnyPending(),std::memory_order_release);
}
#include "nr_source_views.inl"
void OnDestroyResource(ID3D12Resource* resource){if(!resource)return;Lock lock;const auto address=reinterpret_cast<Id>(resource);
    // Destruction is an address retirement receipt, not an occasion to call
    // GetDevice/GetPrivateData through an object whose teardown has started.
    lineage.ForgetAddress(address);
    RetireApplicationResource(address);
    ForgetSourceRtvResource(address);
    for(auto& state:resource_states)if(state.resource.address==address)state={};
}
struct RenderTargets {Recording recording{};Resource targets[8]{};unsigned count=0;};
RenderTargets render_targets[64]{};
void OnBindTargets(reshade::api::command_list* commands,unsigned count,const reshade::api::resource_view* views,reshade::api::resource_view) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed)||!commands||count>8||(!views&&count)||
       commands->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    Lock lock;const auto recording=RecordingOf(reinterpret_cast<ID3D12GraphicsCommandList*>(commands->get_native()));
    if(!recording)return;RenderTargets* target=nullptr;
    for(auto& entry:render_targets){if(entry.recording.list==recording.list&&entry.recording.lifetime==recording.lifetime){target=&entry;break;}
        if(!entry.recording&&!target)target=&entry;}
    if(!target)return;*target={};target->recording=recording;target->count=count;
    for(unsigned i=0;i<count;++i){const auto* view=FindSourceRtv(views[i].handle,recording.device);
        if(view)target->targets[i]=view->resource;}
}
void OnDraw(ID3D12GraphicsCommandList* list) {
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;Lock lock;const auto recording=RecordingOf(list);
    // Unknown UAV side effects invalidate old provenance, then the actual
    // bound RTVs receive fresh output-content generations for this draw.
    lineage.InvalidateDevice(recording);
    for(const auto& targets:render_targets)if(targets.recording==recording){
        for(unsigned i=0;i<targets.count;++i)if(targets.targets[i])lineage.Invalidate(recording,targets.targets[i]);break;}
    if(lineage.PendingList(recording))source_receipts_pending.store(true,std::memory_order_release);
}
void ObserveSurface(reshade::api::swapchain* chain,bool initialize=false) {
    if(!chain||chain->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    const auto color=chain->get_color_space();const unsigned count=chain->get_back_buffer_count();if(count>16)return;
    std::array<Resource,16> resources{};
    for(unsigned i=0;i<count;++i){ID3D12Resource* buffer=nullptr;if(!NrAcquireSwapchainBuffer(chain,i,&buffer))continue;
        const auto format=ID3D12Resource_GetDesc(buffer).Format;unsigned encoding=0;
        if(color==reshade::api::color_space::srgb&&(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_R10G10B10A2_UNORM))encoding=1;
        if(color==reshade::api::color_space::scrgb&&format==DXGI_FORMAT_R16G16B16A16_FLOAT)encoding=2;
        if(color==reshade::api::color_space::hdr10_st2084&&format==DXGI_FORMAT_R10G10B10A2_UNORM)encoding=3;
        {Lock lock;resources[i]=ResourceOf(buffer);lineage.DescribeSurface(resources[i],encoding);}ReleaseCom(buffer);}
    if(initialize){Lock lock;RegisterApplicationSurface(reinterpret_cast<Id>(chain),reinterpret_cast<Id>(chain->get_device()),resources,count);}
}
void OnInitSurface(reshade::api::swapchain* chain,bool){ObserveSurface(chain,true);}
void OnSourceDestroyDevice(reshade::api::device* device){Lock lock;RetireApplicationDevice(reinterpret_cast<Id>(device));}
void OnSourceDestroyResource(reshade::api::device* device,reshade::api::resource resource){
    if(device&&device->get_api()==reshade::api::device_api::d3d12)OnDestroyResource(reinterpret_cast<ID3D12Resource*>(resource.handle));
}
void OnSourceDestroyList(reshade::api::command_list* list){
    if(!list||list->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    const auto address=list->get_native();GuideCopy discarded[8]{};unsigned count=0;
    {Lock lock;lineage.ForgetList(address);ForgetObservedList(address);
        for(auto& state:resource_states)if(state.recording.list==address)state={};
        for(auto& targets:render_targets)if(targets.recording.list==address)targets={};
        for(auto& copy:guide_copies)if(copy.recording.list==address&&!copy.submitted&&!copy.poisoned&&!copy.reserved){discarded[count++]=copy;copy={};}
    }
    for(unsigned i=0;i<count;++i)ClearGuideCopy(discarded[i]);
}
void SetStreamlineEnabled(bool);
void Register(bool enable){
    SetStreamlineEnabled(enable);
    if(enable){reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
        reshade::register_event<reshade::addon_event::init_command_list>(OnSourceInitList);
        reshade::register_event<reshade::addon_event::reset_command_list>(OnSourceResetList);
        reshade::register_event<reshade::addon_event::init_resource_view>(OnSourceInitView);
        reshade::register_event<reshade::addon_event::destroy_resource_view>(OnSourceDestroyView);
        reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSurface);
        reshade::register_event<reshade::addon_event::destroy_resource>(OnSourceDestroyResource);
        reshade::register_event<reshade::addon_event::destroy_device>(OnSourceDestroyDevice);
        reshade::register_event<reshade::addon_event::destroy_command_list>(OnSourceDestroyList);}
    else {reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindTargets);
        reshade::unregister_event<reshade::addon_event::init_command_list>(OnSourceInitList);
        reshade::unregister_event<reshade::addon_event::reset_command_list>(OnSourceResetList);
        reshade::unregister_event<reshade::addon_event::init_resource_view>(OnSourceInitView);
        reshade::unregister_event<reshade::addon_event::destroy_resource_view>(OnSourceDestroyView);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSurface);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(OnSourceDestroyResource);
        reshade::unregister_event<reshade::addon_event::destroy_device>(OnSourceDestroyDevice);
        reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnSourceDestroyList);}
}
bool CanPresent(reshade::api::command_queue* queue,reshade::api::swapchain* chain,ID3D12Resource* buffer) {
    CollectGuideInputs();
    present_lease={};
    PresentObservation diagnostic{};
    if(!queue||!chain||!buffer){diagnostic.decision=PresentDecision::InvalidArguments;PublishPresentObservation(diagnostic);return false;}
    if(!chain->get_device()||chain->get_device()->get_api()!=reshade::api::device_api::d3d12){
        diagnostic.decision=PresentDecision::UnsupportedApi;PublishPresentObservation(diagnostic);return false;
    }
    ObserveSurface(chain);
    Lock lock;
    ReleasePresentGuides(nullptr,0,present_recorded);
    if(present_ticket){if(present_recorded)claims.RetainRecorded(present_ticket);else claims.RejectBeforeWork(present_ticket);present_ticket={};}
    present_recorded=false;
    const auto queue_identity=QueueOf(reinterpret_cast<ID3D12CommandQueue*>(queue->get_native()));diagnostic.queue_identity=queue_identity;
    if(!queue_identity){diagnostic.decision=PresentDecision::QueueIdentityUnavailable;PublishPresentObservationLocked(diagnostic);return false;}
    present_resource=ResourceOf(buffer);auto label=lineage.Lookup(present_resource);
    diagnostic.lookup=label;diagnostic.native_producer_on_device=NativeProducerOnDevice(present_resource.device);
    diagnostic.promotion_eligible=nr_source_frame::PresentPromotionEligible(label,queue_identity);
    diagnostic.promotion_blocked_by_native_producer=false;
    const auto desc=ID3D12Resource_GetDesc(buffer);
    present_demands.Publish(reinterpret_cast<Id>(chain),present_resource.device,
        g_config_generation.load(std::memory_order_relaxed),
        {desc.Width<=UINT_MAX?static_cast<unsigned>(desc.Width):0,desc.Height});
    const bool known_generated=label.role==Role::Generated;
    const bool known_fg_consumed=label.fg_consumed;
    const bool known_inline_processed=label.processed;
    if(label.KnownApplication()&&nr_processing_runtime::EnergyGateBlocks(
        {present_resource.device,label.source.stream,label.source.epoch})) {
        PublishPresentObservationLocked(diagnostic);return false;
    }
    auto selected=AcquirePresentGuides(present_resource.device,desc.Width<=UINT_MAX?static_cast<unsigned>(desc.Width):0,
        desc.Height,known_generated,known_fg_consumed,known_inline_processed,queue_identity,
        g_config_generation.load(std::memory_order_relaxed));
    if(!selected&&AcquirePresentColorOnly(label,present_resource,
        desc.Width<=UINT_MAX?static_cast<unsigned>(desc.Width):0,desc.Height,queue_identity,
        g_config_generation.load(std::memory_order_relaxed)))
        selected={0,nr_late_present::Decision::Accepted,0,0};
    if(!selected&&AcquireApplicationPresentColor(reinterpret_cast<Id>(chain),label,present_resource,
        desc.Width<=UINT_MAX?static_cast<unsigned>(desc.Width):0,desc.Height,queue_identity,
        g_config_generation.load(std::memory_order_relaxed)))
        selected={0,nr_late_present::Decision::Accepted,0,0};
    diagnostic.receipt_decision=static_cast<unsigned>(selected.decision);diagnostic.receipt_age=selected.age;
    diagnostic.receipt_device_age=selected.device_age;
    diagnostic.label=label;
    if(!selected){
        if(known_generated)diagnostic.reject_mask|=nr_source_frame::PresentRejectNoApplication;
        if(known_fg_consumed)diagnostic.reject_mask|=nr_source_frame::PresentRejectFgConsumed;
        if(known_inline_processed)diagnostic.reject_mask|=nr_source_frame::PresentRejectProcessed;
        diagnostic.reject_mask|=nr_source_frame::PresentRejectReceipt;
        diagnostic.decision=PresentDecisionFor(diagnostic);PublishPresentObservationLocked(diagnostic);return false;
    }
    diagnostic.receipt_stream=present_lease.source.stream;diagnostic.receipt_epoch=present_lease.source.epoch;
    if(nr_processing_runtime::EnergyGateBlocks({present_resource.device,
        present_lease.source.stream,present_lease.source.epoch})) {
        // No claim or new NR work. The normal Present-lease cleanup remains
        // responsible for its resources; game presentation is not modified.
        PublishPresentObservationLocked(diagnostic);return false;
    }
    diagnostic.receipt_sequence=present_lease.source.sequence;diagnostic.receipt_queue=present_lease.producer_queue;
    diagnostic.receipt_fence=present_lease.producer_value;diagnostic.receipt_output_width=present_lease.output_width;
    diagnostic.receipt_output_height=present_lease.output_height;diagnostic.receipt_cross_queue_wait=present_lease.cross_queue_wait;
    const auto& source_signature=present_lease.source_signature;
    diagnostic.receipt_depth_x=source_signature.depth_x;diagnostic.receipt_depth_y=source_signature.depth_y;
    diagnostic.receipt_depth_width=source_signature.depth_width;diagnostic.receipt_depth_height=source_signature.depth_height;
    diagnostic.receipt_motion_x=source_signature.motion_x;diagnostic.receipt_motion_y=source_signature.motion_y;
    diagnostic.receipt_motion_width=source_signature.motion_source_width;diagnostic.receipt_motion_height=source_signature.motion_source_height;
    diagnostic.receipt_motion_scale_x=source_signature.motion_scale_x;diagnostic.receipt_motion_scale_y=source_signature.motion_scale_y;
    diagnostic.receipt_game_reset=present_lease.guides.game_reset!=0;
    diagnostic.receipt_source_jitter_known=present_lease.source_jitter_known;
    diagnostic.receipt_output_jitter_known=present_lease.output_jitter_known;
    Label receipt_label{present_lease.source,Role::Application};receipt_label.submitted=true;
    receipt_label.queue=present_lease.producer_queue;diagnostic.label=receipt_label;
    present_ticket=claims.Begin(receipt_label,Point::Present);
    if(!present_ticket){diagnostic.claim_reason=static_cast<Id>(claims.reason);diagnostic.decision=PresentDecision::ClaimRejected;
        PublishPresentObservationLocked(diagnostic);return false;}
    diagnostic.claim_started=true;diagnostic.accepted=true;diagnostic.decision=PresentDecision::Accepted;
    PublishPresentObservationLocked(diagnostic);return true;
}
Id PresentSourceId(){return present_lease.source_id;}
Id PresentRetryToken(){Lock lock;return RetryToken(present_lease.source);}
const PresentLease& CurrentPresentLease(){return present_lease;}
bool MarkPresentRecorded(ID3D12CommandQueue* queue){Lock lock;return BeginPresentReceiptWork(queue);}
void CompletePresent(ID3D12Fence* fence,Id value) {
    if(!present_ticket)return;Lock lock;
    if(!fence||!value)return;
    ReleasePresentGuides(fence,value,present_recorded);
    if(present_recorded){Label label{present_lease.source,Role::Application};label.content=present_lease.source_id;
        lineage.Presented(present_resource,label,present_lease.queue);}
    const auto done=ID3D12Fence_GetCompletedValue(fence);
    claims.Complete(present_ticket,done!=UINT64_MAX,done>=value&&done!=UINT64_MAX);
    // Actual retirement is owned by the Present worker. Preserve the source
    // claim even if its fence has not completed at this CPU call.
    claims.RetainRecorded(present_ticket);present_ticket={};
}
void CancelPresentBeforeWork(){Lock lock;ReleasePresentGuides(nullptr,0,present_recorded);if(present_ticket){claims.RejectBeforeWork(present_ticket);present_ticket={};}}
#include "nr_fg_streamline.inl"
void SetStreamlineEnabled(bool enable){sl_wire::enabled.store(enable,std::memory_order_release);if(enable)PollStreamline();}
} // namespace nr_source_runtime
