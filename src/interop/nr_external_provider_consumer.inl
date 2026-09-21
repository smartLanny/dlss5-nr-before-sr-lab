// Stable external Feeder/provider wire translated into the D15 SourceFrame
// owner. This file owns only the CPU ABI handshake and exact completion
// receipts. All NR recording goes through nr_fg_runtime::ConsumeDetailed and
// the same nr_source_frame::Claims instance used by native/FG/Present paths.
#include "nr_external_provider.h"
#include "nr_external_provider_feedback.h"
#include "nr_external_observer_startup.inl"

namespace nr_external_provider_runtime {
SRWLOCK lock=SRWLOCK_INIT;
SRWLOCK operation_lock=SRWLOCK_INIT;
nr_external_provider::Session session{};

struct OperationGuard {
    bool acquired=false;
    explicit OperationGuard(bool wait){
        if(wait){AcquireSRWLockExclusive(&operation_lock);acquired=true;}
        else acquired=TryAcquireSRWLockExclusive(&operation_lock)!=FALSE;
    }
    ~OperationGuard(){if(acquired)ReleaseSRWLockExclusive(&operation_lock);}
};

struct PendingCompletion {
    nr_source_frame::Ticket ticket{};
    ID3D12Fence* fence=nullptr;std::uint64_t value=0,provider=0;
};
PendingCompletion pending[32]{};

void RefreshPendingCount(){
    unsigned count=0;for(const auto& item:pending)count+=item.ticket?1u:0u;
    nr_external_provider_feedback::pending_completions.store(count,std::memory_order_relaxed);
}
void PollCompletions(){
    for(auto& item:pending){
        if(!item.ticket||!item.fence||!item.value)continue;
        const auto done=ID3D12Fence_GetCompletedValue(item.fence);
        if(done!=UINT64_MAX&&done<item.value)continue;
        {
            nr_source_runtime::Lock source_lock;
            nr_source_runtime::claims.Complete(item.ticket,done!=UINT64_MAX,done!=UINT64_MAX&&done>=item.value);
        }
        if(done==UINT64_MAX){
            ++nr_external_provider_feedback::completion_failures;
            session.history_dirty=true;session.last_bypass=NrExternalProviderReason::TransportFailed;
        }else ++nr_external_provider_feedback::completion_frames;
        ReleaseCom(item.fence);
        item={};
    }
    RefreshPendingCount();
}
bool HasPending(std::uint64_t provider){
    for(const auto& item:pending)if(item.ticket&&item.provider==provider)return true;
    return false;
}
PendingCompletion* FreePending(){for(auto& item:pending)if(!item.ticket)return &item;return nullptr;}

nr_external_provider::ConfigSnapshot Snapshot(){
    nr_external_provider::ConfigSnapshot cfg{};
    const auto epoch=g_module_observer_epoch.load(std::memory_order_acquire);cfg.observer_epoch=epoch;
    cfg.observer_uncertain=(epoch&1u)||g_module_rescan_in_progress.load(std::memory_order_acquire)||
        g_module_observer_gap_detected.load(std::memory_order_acquire)||g_feature_tracking_uncertain.load(std::memory_order_acquire);
    cfg.competing_nr=LatchGenericRenoDxConflictIfLoaded()||g_shortfuse_nr_enabled.load(std::memory_order_acquire);
    AcquireSRWLockShared(&g_feature_lock);
    for(const auto& feature:g_features)if(feature.handle&&(feature.feature_id==1||feature.feature_id==13||feature.feature_id==11)){
        cfg.competing_nr=true;break;
    }
    ReleaseSRWLockShared(&g_feature_lock);
    const auto current=SnapshotConfig();cfg.master_enabled=current.enabled;
    cfg.transfer_strength=ActiveTransferStrength(current);return cfg;
}
void PublishBypass(NrExternalProviderReason reason){
    switch(reason){
    case NrExternalProviderReason::Disabled:SetBypassReason(BypassReason::MasterDisabled);break;
    case NrExternalProviderReason::CompetingNr:SetBypassReason(BypassReason::ExternalNrOwner);break;
    case NrExternalProviderReason::MissingRequired:
    case NrExternalProviderReason::UnsupportedContract:SetBypassReason(BypassReason::UnsupportedInputFormat);break;
    case NrExternalProviderReason::LeaseInvalid:
    case NrExternalProviderReason::TransportFailed:SetBypassReason(BypassReason::QueueAffinityUnverified);break;
    case NrExternalProviderReason::RuntimeUnavailable:SetBypassReason(BypassReason::RuntimeUnavailable);break;
    case NrExternalProviderReason::ObserverUncertain:SetBypassReason(BypassReason::ModuleObserverUncertain);break;
    case NrExternalProviderReason::IdentityPassthrough:
    case NrExternalProviderReason::Ready:SetBypassReason(BypassReason::None);break;
    default:break;
    }
}

ID3D12Resource* Resource(std::uint64_t value){return reinterpret_cast<ID3D12Resource*>(static_cast<std::uintptr_t>(value));}
ID3D12GraphicsCommandList* List(std::uint64_t value){return reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<std::uintptr_t>(value));}
ID3D12CommandQueue* Queue(std::uint64_t value){return reinterpret_cast<ID3D12CommandQueue*>(static_cast<std::uintptr_t>(value));}
ID3D12Fence* Fence(std::uint64_t value){return reinterpret_cast<ID3D12Fence*>(static_cast<std::uintptr_t>(value));}

bool SameDevice(ID3D12Device* device,ID3D12DeviceChild* object){
    ID3D12Device* owner=nullptr;const bool same=object&&SUCCEEDED(ID3D12DeviceChild_GetDevice(object,IID_ID3D12Device,reinterpret_cast<void**>(&owner)))&&
        owner&&IsSameRrDepthDevice(device,owner);ReleaseCom(owner);return same;
}
bool ResourceMatches(const NrExternalResourceV1& wire,bool required){
    if(!wire.resource)return !required&&wire.provenance==NrGuideProvenance::Unknown;
    if(!nr_external_provider::ResourcePresent(wire)||!nr_external_provider::ResourceRectInside(wire)||wire.subresource||
       wire.state_before!=wire.state_after)return false;
    const auto desc=ID3D12Resource_GetDesc(Resource(wire.resource));
    return desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc.Width==wire.width&&desc.Height==wire.height&&
        desc.Format==static_cast<DXGI_FORMAT>(wire.format)&&desc.MipLevels==1&&desc.DepthOrArraySize==1&&desc.SampleDesc.Count==1;
}
bool VerifyLease(const NrExternalFrameV1& frame,NrExternalProviderReason& reason,ID3D12Device*& device){
    reason=NrExternalProviderReason::LeaseInvalid;device=nullptr;
    auto* list=List(frame.command_list);auto* queue=Queue(frame.queue);auto* fence=Fence(frame.fence);
    const auto known_flags=static_cast<std::uint32_t>(NrExternalLeaseFlags::CommandListOwnedByProducer)|
        static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired)|static_cast<std::uint32_t>(NrExternalLeaseFlags::OutputSameAsColor)|
        static_cast<std::uint32_t>(NrExternalLeaseFlags::ResetTemporal)|static_cast<std::uint32_t>(NrExternalLeaseFlags::DepthInverted)|
        static_cast<std::uint32_t>(NrExternalLeaseFlags::HdrColor);
    if((frame.lease_flags&~known_flags)||!list||!queue||!fence||!frame.fence_value||frame.fence_value==UINT64_MAX||
       (frame.lease_flags&static_cast<std::uint32_t>(NrExternalLeaseFlags::CommandListOwnedByProducer))==0||
       (frame.lease_flags&static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired))==0||
       ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_DIRECT||ID3D12CommandQueue_GetDesc(queue).Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)
        return false;
    if(ID3D12Fence_GetCompletedValue(fence)==UINT64_MAX||ID3D12Fence_GetCompletedValue(fence)>=frame.fence_value)return false;
    // The reused transport does not consume a live exposure texture or apply
    // its scale. Reject instead of silently treating non-unity HDR as unity.
    if(frame.exposure.resource||!std::isfinite(frame.pre_exposure)||!std::isfinite(frame.exposure_scale)||
       frame.pre_exposure!=1.0f||frame.exposure_scale!=1.0f){reason=NrExternalProviderReason::UnsupportedContract;return false;}
    if(!ResourceMatches(frame.color,true)||!ResourceMatches(frame.output,true)||
       !ResourceMatches(frame.depth,true)||!ResourceMatches(frame.motion,true))return false;
    if(frame.depth.provenance==NrGuideProvenance::Unknown||frame.motion.provenance==NrGuideProvenance::Unknown||
       (frame.motion.motion_unit!=NrExternalMotionUnit::Pixels&&frame.motion.motion_unit!=NrExternalMotionUnit::Normalized)||
       !std::isfinite(frame.motion.motion_scale_x)||!std::isfinite(frame.motion.motion_scale_y)||
       std::abs(frame.motion.motion_scale_x)<=0.000001f||std::abs(frame.motion.motion_scale_y)<=0.000001f)return false;
    if(FAILED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return false;
    for(auto* object:{reinterpret_cast<ID3D12DeviceChild*>(queue),reinterpret_cast<ID3D12DeviceChild*>(fence),
        reinterpret_cast<ID3D12DeviceChild*>(Resource(frame.color.resource)),reinterpret_cast<ID3D12DeviceChild*>(Resource(frame.output.resource)),
        reinterpret_cast<ID3D12DeviceChild*>(Resource(frame.depth.resource)),reinterpret_cast<ID3D12DeviceChild*>(Resource(frame.motion.resource))})
        if(!SameDevice(device,object)){ReleaseCom(device);return false;}
    if(!EnsureNativeQueueExecuteHookForNative(queue)||!EnsureNativeCommandListResetHookForNative(list)){
        ReleaseCom(device);reason=NrExternalProviderReason::RuntimeUnavailable;return false;
    }
    const auto coverage=EnsurePostBoundaryNativeHooks(list);
    const auto required=nr_post_boundary::Legacy|nr_post_boundary::Operations;
    if((coverage&required)!=required||(coverage&(nr_post_boundary::HookMissing|nr_post_boundary::InterfaceUnknown|nr_post_boundary::IdentityUnknown))){
        ReleaseCom(device);reason=NrExternalProviderReason::RuntimeUnavailable;return false;
    }
    bool trained=false;
    EnterCriticalSection(&g_chain_cs);
    auto* tracked=TrackGraphicsQueueLocked(queue);CommandListQueueMarker marker{};
    trained=tracked&&tracked->identity&&ReadLastSubmissionQueueMarker(list,marker)&&marker.identity==tracked->identity&&
        marker.stable_submissions>=kRequiredStableQueueSubmissions&&(marker.flags&kQueueMarkerMigrated)==0;
    LeaveCriticalSection(&g_chain_cs);
    if(!trained){NrExternalSetTraining(list,queue);ReleaseCom(device);return false;}
    NrExternalSetTraining(nullptr,nullptr);
    reason=NrExternalProviderReason::Ready;return true;
}

bool Translate(const NrExternalFrameV1& frame,bool history_reset,ID3D12Device* device,nr_source_runtime::NativeInputs& input){
    input={};
    input.external=true;
    const auto color_format=static_cast<DXGI_FORMAT>(frame.color.format);
    if(color_format==DXGI_FORMAT_R8G8B8A8_UNORM||color_format==DXGI_FORMAT_B8G8R8A8_UNORM)input.encoding=1;
    // R16G16B16A16_FLOAT is the wire storage contract for both linear SDR
    // transports (including the Vulkan decode bridge) and scRGB/HDR. HdrColor
    // describes the signal range; it must not be used as a format-validity bit.
    else if(color_format==DXGI_FORMAT_R16G16B16A16_FLOAT)input.encoding=2;
    else if(color_format==DXGI_FORMAT_R10G10B10A2_UNORM&&(frame.lease_flags&static_cast<unsigned>(NrExternalLeaseFlags::HdrColor)))input.encoding=3;
    else return false;
    if(frame.color.format!=frame.output.format||frame.color.rect_x||frame.color.rect_y||frame.output.rect_x||frame.output.rect_y||
       frame.color.rect_w!=frame.color.width||frame.color.rect_h!=frame.color.height||frame.output.rect_w!=frame.output.width||
       frame.output.rect_h!=frame.output.height||frame.color.rect_w!=frame.output.rect_w||frame.color.rect_h!=frame.output.rect_h)return false;
    auto* output=Resource(frame.output.resource);if((ID3D12Resource_GetDesc(output).Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)==0)return false;
    nr_source_frame::Id device_id=0;{nr_source_runtime::Lock source_lock;device_id=nr_source_runtime::ObjectId(reinterpret_cast<ID3D12Object*>(device));}
    input.source=nr_source_runtime::PublishExternalSource(frame.provider_id,frame.view_id,frame.epoch,frame.frame_id,device_id);
    if(!input.source)return false;
    input.color=Resource(frame.color.resource);input.depth=Resource(frame.depth.resource);input.motion=Resource(frame.motion.resource);input.output=output;
    input.color_rect={frame.color.rect_x,frame.color.rect_y,frame.color.rect_w,frame.color.rect_h};
    input.depth_rect={frame.depth.rect_x,frame.depth.rect_y,frame.depth.rect_w,frame.depth.rect_h};
    input.motion_rect={frame.motion.rect_x,frame.motion.rect_y,frame.motion.rect_w,frame.motion.rect_h};
    input.output_rect={frame.output.rect_x,frame.output.rect_y,frame.output.rect_w,frame.output.rect_h};
    input.motion_scale_x=frame.motion.motion_scale_x;input.motion_scale_y=frame.motion.motion_scale_y;
    input.motion_scale_known=true;input.motion_scale_normalized=frame.motion.motion_unit==NrExternalMotionUnit::Normalized;
    input.jitter_x=frame.motion.jitter_x;input.jitter_y=frame.motion.jitter_y;
    input.jitter_known=std::isfinite(input.jitter_x)&&std::isfinite(input.jitter_y)&&
        !(frame.motion.provenance==NrGuideProvenance::Synthetic&&input.jitter_x==0.0f&&input.jitter_y==0.0f);
    input.depth_inverted=(frame.lease_flags&static_cast<unsigned>(NrExternalLeaseFlags::DepthInverted))!=0;
    input.hdr=(frame.lease_flags&static_cast<unsigned>(NrExternalLeaseFlags::HdrColor))!=0;
    input.reset=history_reset||(frame.lease_flags&static_cast<unsigned>(NrExternalLeaseFlags::ResetTemporal))!=0;
    input.states_known=true;input.output_state_known=true;
    input.color_state=frame.color.state_before;input.depth_state=frame.depth.state_before;input.motion_state=frame.motion.state_before;input.output_state=frame.output.state_before;
    return true;
}
} // namespace nr_external_provider_runtime

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_QueryV1(
    std::uint32_t abi,std::uint32_t size,std::uint32_t flags,NrExternalStatusV1* output){
    if(abi!=kNrExternalProviderAbi||size!=sizeof(*output)||!output||(flags&~3u))return 0;
    if(flags&kNrExternalQueryNativeObserver){*output=QueryNativeObserverStartup();return 1;}
    nr_external_provider_runtime::OperationGuard operation(false);
    if(!operation.acquired){*output={};output->reason=NrExternalProviderReason::Busy;return 0;}
    nr_external_provider_runtime::PollCompletions();if(flags&1u)PollNgxLifecycleModules(true);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);auto cfg=nr_external_provider_runtime::Snapshot();
    nr_external_provider::FillStatus(nr_external_provider_runtime::session,cfg,*output);nr_external_provider_feedback::Publish(*output);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);return 1;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_ClaimOwnershipV1(
    std::uint32_t abi,std::uint32_t size,std::uint64_t provider,NrExternalStatusV1* output){
    if(abi!=kNrExternalProviderAbi||size!=sizeof(*output)||!output||!provider)return 0;
    nr_external_provider_runtime::OperationGuard operation(true);nr_external_provider_runtime::PollCompletions();
    EnterCriticalSection(&g_chain_cs);AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    auto cfg=nr_external_provider_runtime::Snapshot();
    if(!nr_external_provider_runtime::session.owned){
        bool busy=false;
        for(const auto& context:g_contexts)busy|=!CanDestroyContextLocked(context);
        for(const auto& context:g_pending_destroy_contexts)busy|=!CanDestroyContextLocked(context);
        if(busy){*output={};output->reason=NrExternalProviderReason::Busy;
            ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);LeaveCriticalSection(&g_chain_cs);return 0;}
    }
    const bool ok=nr_external_provider::ClaimOwnership(nr_external_provider_runtime::session,cfg,provider);
    nr_external_provider::FillStatus(nr_external_provider_runtime::session,cfg,*output);nr_external_provider_feedback::Publish(*output);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);LeaveCriticalSection(&g_chain_cs);return ok?1:0;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_ReleaseOwnershipV1(
    std::uint32_t abi,std::uint64_t provider){
    if(abi!=kNrExternalProviderAbi||!provider)return 0;
    nr_external_provider_runtime::OperationGuard operation(true);nr_external_provider_runtime::PollCompletions();
    AcquireSRWLockShared(&nr_external_provider_runtime::lock);
    const bool owned=nr_external_provider_runtime::session.owned&&nr_external_provider_runtime::session.owner_provider_id==provider;
    ReleaseSRWLockShared(&nr_external_provider_runtime::lock);
    if(!owned||nr_external_provider_runtime::HasPending(provider))return 0;
    nr_source_runtime::RetireExternalProvider(provider);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    nr_external_provider::ReleaseOwnership(nr_external_provider_runtime::session,provider);NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session,nr_external_provider_runtime::Snapshot(),status);
    nr_external_provider_feedback::Publish(status);ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    NrExternalSetTraining(nullptr,nullptr);nr_fg_runtime::CollectRetired();return 1;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_SubmitFrameV1(
    std::uint32_t abi,const NrExternalFrameV1* frame,NrExternalSubmitResultV1* result){
    if(abi!=kNrExternalProviderAbi||!frame||!result||frame->size!=sizeof(*frame)||frame->abi!=kNrExternalProviderAbi)return 0;
    nr_external_provider_runtime::OperationGuard operation(false);
    if(!operation.acquired){*result={};result->reason=NrExternalProviderReason::Busy;return 0;}
    nr_external_provider_runtime::PollCompletions();
    // Ownership and OFF are CPU decisions. A caller without the current lease
    // must not arm training or cause resource/queue inspection.
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    const auto preflight=nr_external_provider_runtime::Snapshot();
    auto& live=nr_external_provider_runtime::session;
    if(!nr_external_provider::FrameHeaderOk(*frame)||!live.owned||live.owner_provider_id!=frame->provider_id||
       !nr_external_provider::StrengthAllowsWork(preflight)||preflight.competing_nr||preflight.observer_uncertain){
        const auto decision=nr_external_provider::Admit(live,preflight,*frame);
        nr_external_provider::FillSubmitResult(decision,*frame,false,*result);
        ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);return decision.admit?1:0;
    }
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    if(!nr_external_provider_runtime::FreePending()){*result={};result->reason=NrExternalProviderReason::Busy;return 0;}
    NrExternalProviderReason lease_reason{};ID3D12Device* device=nullptr;
    if(!nr_external_provider_runtime::VerifyLease(*frame,lease_reason,device)){
        *result={};result->reason=lease_reason;nr_external_provider_runtime::PublishBypass(lease_reason);return 0;
    }
    nr_external_provider::AdmitDecision admit{};nr_external_provider::Session before{};nr_external_provider::ConfigSnapshot cfg{};
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);cfg=nr_external_provider_runtime::Snapshot();before=nr_external_provider_runtime::session;
    admit=nr_external_provider::Admit(nr_external_provider_runtime::session,cfg,*frame);
    if(!admit.admit||!admit.run_nr){nr_external_provider::FillSubmitResult(admit,*frame,false,*result);
        if(admit.admit)++nr_external_provider_feedback::external_submits;NrExternalStatusV1 status{};
        nr_external_provider::FillStatus(nr_external_provider_runtime::session,cfg,status);nr_external_provider_feedback::Publish(status);
        nr_external_provider_runtime::PublishBypass(admit.reason);ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);ReleaseCom(device);return admit.admit?1:0;}
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    nr_source_runtime::NativeInputs input{};bool writeback=false,recorded=false;nr_source_frame::Ticket ticket{};NgxResult fatal=0;
    if(nr_external_provider_runtime::Translate(*frame,admit.history_reset,device,input)){
        nr_source_runtime::EvaluateScope scope(nr_external_provider_runtime::List(frame->command_list),input.source,input);
        recorded=nr_fg_runtime::ConsumeDetailed(nr_external_provider_runtime::List(frame->command_list),input,&writeback);
        fatal=scope.FatalResult();if(recorded&&!fatal)ticket=scope.DeferRecordedCompletion();
    }
    ReleaseCom(device);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    if(recorded&&ticket&&!fatal){
        auto* pending=nr_external_provider_runtime::FreePending();auto* fence=nr_external_provider_runtime::Fence(frame->fence);
        ID3D12Fence_AddRef(fence);*pending={ticket,fence,frame->fence_value,frame->provider_id};
        nr_external_provider_runtime::RefreshPendingCount();admit.reason=writeback?NrExternalProviderReason::Ready:NrExternalProviderReason::IdentityPassthrough;
        if(writeback)nr_external_provider::NoteWriteback(nr_external_provider_runtime::session,true);
        else {nr_external_provider_runtime::session.last_bypass=admit.reason;++nr_external_provider_runtime::session.identity_frames;}
        nr_external_provider_feedback::nr_backend.store(3,std::memory_order_relaxed);
    }else{
        admit.reason=fatal?NrExternalProviderReason::ObserverFailed:NrExternalProviderReason::RuntimeUnavailable;
        admit.admit=admit.run_nr=false;nr_external_provider::RollbackAdmit(nr_external_provider_runtime::session,before,admit.reason);
    }
    nr_external_provider::FillSubmitResult(admit,*frame,writeback&&ticket&&!fatal,*result);++nr_external_provider_feedback::external_submits;
    NrExternalStatusV1 status{};nr_external_provider::FillStatus(nr_external_provider_runtime::session,cfg,status);nr_external_provider_feedback::Publish(status);
    nr_external_provider_runtime::PublishBypass(admit.reason);ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    return recorded&&ticket&&!fatal?1:0;
}
