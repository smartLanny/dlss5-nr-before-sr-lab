// Complete production TU, actual ticket/publish/select/readback functions and
// native MinHook detours. All COM resources are CPU fixtures; no D3D device.
namespace boundary_readonly_test {
using queue_test::Check;
std::uint32_t readback[kDiagnosticStatsUintCount]{};
unsigned maps=0,unmaps=0;
HRESULT STDMETHODCALLTYPE Map(ID3D12Resource *,UINT,const D3D12_RANGE *,void **data) {
    ++maps;*data=readback;return S_OK;
}
void STDMETHODCALLTYPE Unmap(ID3D12Resource *,UINT,const D3D12_RANGE *) {++unmaps;}
void Run() {
    using namespace nr_post_boundary;
    boundary_native_test::Initialize();
    auto context=std::make_unique<NrContext>();auto &slot=context->slots[0];
    context->instance_id=10;context->config_generation=9;context->feature_generation=3;
    context->mode=1;context->evaluations=120;
    slot.result=reinterpret_cast<ID3D12Resource *>(0x1010);
    slot.native_format_key={17,101,3,nr_native_format::Role::Output};slot.recovery_target={101,101,0};
    slot.snapshot_heap=reinterpret_cast<ID3D12DescriptorHeap *>(0x2020);
    g_codec.post_store_probe=reinterpret_cast<ID3D12PipelineState *>(0x3030);
    PostSrSubstitution substitution;substitution.active=true;substitution.context=context.get();
    substitution.slot=&slot;substitution.original_output=slot.result;
    const auto normal=NrProbePolicySnapshot();
    Check(normal.phase==1 && normal.AutomaticSampling(),"readonly fixture starts normal phase");
    ArmPostBoundaryObservationLocked(substitution,normal);
    Check(!substitution.boundary_ticket,"normal boundary retains GPU capture gate");
    slot.delta_stats_capture_armed=true;ArmPostBoundaryObservationLocked(substitution,normal);
    Check(static_cast<bool>(substitution.boundary_ticket),"normal existing armed store enables boundary");
    CompleteNrDiagnosticEvaluation(*context,slot);const auto first=g_applied_nr_identity;
    auto &metadata=slot.stored_result_metadata;
    metadata.identity=first;metadata.native_key=slot.native_format_key;metadata.mode=1;metadata.recorded=true;
    metadata.feature_generation=3;metadata.probe_policy=normal;metadata.recovery_target=slot.recovery_target;
    metadata.recovery_token.key={{17,17,1,3,9,1},101,101,0,1};
    metadata.recovery_token.attempt_id=21;metadata.recovery_token.carrier={81,31};
    slot.recovery_token=metadata.recovery_token;
    nr_descriptor::NativeUse linked;linked.call=41;linked.carrier=91;linked.resource=111;
    linked.semantic_resource=101;linked.device=17;linked.feature=3;linked.list=71;linked.reset=4;
    linked.list_address=0x5050;linked.identity_known=true;linked.finished=true;linked.native_success=true;
    linked.observed_typed=24;linked.matches=1;linked.after_native_valid=true;
    substitution.native_boundary.list={0x5050,71,4,17,true};
    Check(substitution.native_boundary.AttachUse(linked,17,3,101),"same native call binds exact output list reset and feature identities");
    for(unsigned field=0;field<7;++field) {
        auto wrong=linked;
        if(field==0)++wrong.list;else if(field==1)++wrong.reset;else if(field==2)++wrong.list_address;
        else if(field==3)++wrong.device;else if(field==4)++wrong.feature;else if(field==5)++wrong.semantic_resource;
        else wrong.call=0;
        Check(!substitution.native_boundary.AttachUse(wrong,17,3,101) && !substitution.native_boundary.use_call,
              "wrong or unavailable native identity clears the link instead of retaining an earlier success");
    }
    substitution.native_boundary.AttachUse(linked,17,3,101);
    metadata.result_uav=static_cast<unsigned>(DXGI_FORMAT_R10G10B10A2_UNORM);
    slot.post_boundary_probe_recorded=true;
    PublishPostBoundaryRecording(substitution,first,3,0x4444);
    const auto trace_a=slot.post_boundary_trace_id;const auto scope_a=substitution.boundary_ticket.scope;
    ClosePostBoundaryAtPresent(0x4444);
    Check(trace_a && SnapshotPostBoundary(trace_a,first.frame_id,9,3).provenance==Provenance::StoreProbe,
          "production publisher pairs exact normal store identity");
    const auto linked_window=SnapshotPostBoundary(trace_a,first.frame_id,9,3);
    Check(linked_window.recovery.attempt==21 && linked_window.recovery.carrier==81 && linked_window.recovery.ordinal==31 &&
          linked_window.native.use_call==41,
          "published window freezes native call and same-recording recovery attempt without conflating frame domains");
    const StoreProbeReference stored{trace_a,first.frame_id,scope_a};
    Check(SelectPostBoundary(scope_a,stored).kind==SelectionKind::CurrentStoreProbe,"current exact store is paired");
    auto mismatched_identity=first;++mismatched_identity.frame_id;
    const auto before_unpaired=g_post_boundary_next_trace;
    PublishPostBoundaryRecording(substitution,mismatched_identity,3,0x4444);
    Check(g_post_boundary_next_trace==before_unpaired,"normal path cannot pair stale recorded flag with new identity");
    const auto frozen=metadata;const auto frozen_identity=slot.diagnostic_identity;
    const auto history=g_history_generation.load();const auto config=g_config_generation.load();
    SetNrProbePolicy(nr_probe_policy::Mode::Paused);const auto paused=NrProbePolicySnapshot();
    slot.delta_stats_capture_armed=false;slot.snapshot_heap=nullptr;g_codec.post_store_probe=nullptr;
    ArmPostBoundaryObservationLocked(substitution,paused);const auto scope_b=substitution.boundary_ticket.scope;
    Check(substitution.boundary_ticket && scope_b.phase==paused.phase,"paused independent CPU ticket requires no GPU probe resources");
    const auto first_ticket=substitution.boundary_ticket;
    ArmPostBoundaryObservationLocked(substitution,paused);
    Check(!substitution.boundary_ticket,"same evaluation cannot retry unbounded readonly capture");
    substitution.boundary_ticket=first_ticket;
    auto *list=reinterpret_cast<ID3D12GraphicsCommandList *>(&boundary_native_test::object);
    const auto coverage=EnsurePostBoundaryNativeHooks(list);
    const auto native_before=boundary_native_test::forwarded;
    {
        NrNativeOutputTraceScope trace(slot.result,list,coverage);g_inside_ngx_evaluate=true;
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={slot.result,0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
        substitution.native_boundary=trace.Finish();g_inside_ngx_evaluate=false;
    }
    Check(boundary_native_test::forwarded==native_before+1 && substitution.native_boundary.exact_matches==1,
          "paused native transition observed and forwarded exactly once");
    ++context->evaluations;CompleteNrDiagnosticEvaluation(*context,slot);const auto second=g_applied_nr_identity;
    slot.recovery_token.attempt_id=22;slot.recovery_token.carrier={82,32};
    Check(second.frame_id>first.frame_id && slot.diagnostic_identity.frame_id==frozen_identity.frame_id,
          "successful paused evaluation has fresh identity without relabeling old sample");
    const auto before_publish=boundary_native_test::forwarded;
    PublishPostBoundaryRecording(substitution,second,3,0x4444);
    const auto trace_b=g_post_boundary_current.trace;
    Check(trace_b!=trace_a && boundary_native_test::forwarded==before_publish,
          "readonly publish issues no native GPU API");
    for(unsigned i=0;i<kFollowingEvents+2;++i)ID3D12GraphicsCommandList_CopyResource(list,slot.result,nullptr);
    ClosePostBoundaryAtPresent(0x4444);
    auto selected=SelectPostBoundary(scope_b,stored);
    Check(selected.kind==SelectionKind::CurrentReadOnly && !selected.paired_with_probe && selected.store_trace_available &&
          selected.scope_current && selected.phase_current && selected.window.trace==trace_b &&
          selected.window.frame==second.frame_id && selected.window.count==kFollowingEvents && selected.window.dropped==2,
          "paused current recording wins while historical store reference stays distinct and bounded");
    Check(selected.window.recovery.attempt==22 && selected.window.recovery.carrier==82 &&
          SnapshotPostBoundary(trace_a,first.frame_id,9,3).recovery.attempt==21,
          "paused recovery link advances while the original store window retains its own attempt");
    Check(slot.post_boundary_trace_id==trace_a && !std::memcmp(&metadata,&frozen,sizeof(frozen)) &&
          !slot.delta_stats_capture_armed && !slot.delta_stats_pending && config==g_config_generation.load() &&
          history==g_history_generation.load(),"readonly recording preserves GPU sample metadata config and history");
    ID3D12ResourceVtbl resource_table{};resource_table.Map=Map;resource_table.Unmap=Unmap;
    ID3D12Resource resource{&resource_table};slot.delta_stats_readback=&resource;slot.delta_stats_pending=true;
    readback[28]=1;g_lifecycle_tracking_uncertain.store(false);
    CollectSlotDeltaStatsLocked(*context,slot);
    Check(maps==1 && unmaps==1 && !slot.delta_stats_pending && g_post_store_evidence.frame==first.frame_id &&
          g_post_store_evidence.boundary_trace_id==trace_a && g_post_store_evidence.probe_policy.phase==normal.phase,
          "actual late CPU readback publishes original store phase frame and trace");
    auto &receipt=g_nr_recovery_snapshot;receipt={};receipt.key=metadata.recovery_token.key;
    receipt.input.current={true,9,3,1,1};
    auto feedback=std::make_unique<FeedbackSession>();FeedbackAppendActivity(*feedback,"end");
    Check(std::strstr(feedback->report,"end.post.boundary.recoveryAttempt=22\n") &&
          std::strstr(feedback->report,"end.post.boundary.recoveryCarrier=82\n") &&
          std::strstr(feedback->report,"end.post.boundary.recoveryOrdinal=32\n") &&
          std::strstr(feedback->report,"end.post.boundary.nativeUseCall=0\n"),
          "F8 serializes the selected window link and does not borrow the latest native-use snapshot");
    Check(std::strstr(feedback->report,"end.post.boundary.selection=current-read-only-recording") &&
          std::strstr(feedback->report,"end.post.boundary.pairedWithProbe=0") &&
          std::strstr(feedback->report,"end.post.boundary.phaseCurrent=1") &&
          std::strstr(feedback->report,"end.store.post.probePhaseCurrent=0"),
          "production F8 distinguishes new paused recording and late historical GPU sample");
    auto mismatched=scope_b;++mismatched.target;
    Check(SelectPostBoundary(mismatched,{}).kind==SelectionKind::None,"another resource cannot borrow current recording");
    for(unsigned field=0;field<5;++field) {
        mismatched=scope_b;
        if(field==0)++mismatched.device;else if(field==1)++mismatched.feature;else if(field==2)++mismatched.config;
        else if(field==3)++mismatched.subresource;else ++mismatched.phase;
        Check(SelectPostBoundary(mismatched,{}).kind==SelectionKind::None,"complete observation scope must match");
    }
    auto bad_stored=stored;++bad_stored.frame;
    Check(!SelectPostBoundary(scope_b,bad_stored).store_trace_available,"store trace with wrong frame cannot pair");
    for(unsigned i=0;i<kHistory;++i) {
        auto identity=second;identity.frame_id+=i+1;
        PublishPostBoundaryRecording(substitution,identity,3,0x4444);ClosePostBoundaryAtPresent(0x4444);
    }
    selected=SelectPostBoundary(scope_b,stored);
    Check(selected.kind==SelectionKind::CurrentReadOnly && !selected.store_trace_available && !selected.paired_with_probe,
          "evicted old pair is unavailable and never reassigned to newer trace");
    context->evaluations=122;ArmPostBoundaryObservationLocked(substitution,paused);
    Check(!substitution.boundary_ticket,"readonly cadence does not arm every evaluation");
    SetNrProbePolicy(nr_probe_policy::Mode::OutputTransitionsOnly);const auto transitions=NrProbePolicySnapshot();
    ArmPostBoundaryObservationLocked(substitution,transitions);
    Check(substitution.boundary_ticket && substitution.boundary_ticket.scope.phase==transitions.phase &&
          !slot.delta_stats_capture_armed,"state-only phase gets independent first readonly opportunity");
    ++context->evaluations;CompleteNrDiagnosticEvaluation(*context,slot);
    PublishPostBoundaryRecording(substitution,g_applied_nr_identity,3,0x4444);ClosePostBoundaryAtPresent(0x4444);
    selected=SelectPostBoundary(substitution.boundary_ticket.scope,stored);
    Check(selected.kind==SelectionKind::CurrentReadOnly && selected.window.observation.phase==transitions.phase &&
          selected.window.observation.probe_mode==2 && slot.post_boundary_trace_id==trace_a &&
          !slot.delta_stats_capture_armed && !std::memcmp(&metadata,&frozen,sizeof(frozen)),
          "state-only publisher stays readonly and leaves old probe metadata untouched");
    auto stale=substitution;SetNrProbePolicy(nr_probe_policy::Mode::Paused);
    const auto before_stale=g_post_boundary_next_trace;PublishPostBoundaryRecording(stale,second,3,0x4444);
    Check(g_post_boundary_next_trace==before_stale,"phase change during recording discards stale ticket");
    context->mode=2;ArmPostBoundaryObservationLocked(substitution,NrProbePolicySnapshot());
    Check(!substitution.boundary_ticket,"pre route cannot produce post readonly ticket");
    std::printf("Boundary readonly: %u checks; production ticket/publish/select/late-readback/native forwarding; CPU only\n",queue_test::checks);
}
}
