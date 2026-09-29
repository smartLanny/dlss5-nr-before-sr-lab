// Explicit native pre/post compositor seams. Called under the chain owner lock.
// Original/guide/target leases and actual Execute/Signal remain Core-owned.
#include "nr_temporal_route.h"
namespace nr_temporal {
using namespace nr::final_residual;
struct Memory {unsigned live=0,retired=0,pending=0,quarantined=0;unsigned long long live_bytes=0,retired_bytes=0,pending_bytes=0,quarantined_bytes=0;};
}
namespace nr_compat_present_runtime {void AppendTemporalMemory(nr_temporal::Memory&);}
namespace nr_temporal {
Memory SnapshotMemory(){
 Memory out;EnterCriticalSection(&g_chain_cs);
 const auto collect=[&](const NrContext& c,bool pending){if(!c.temporal)return;const auto bytes=c.temporal->stats().bytes;
  if(pending){++out.pending;out.pending_bytes+=bytes;}else if(c.retired){++out.retired;out.retired_bytes+=bytes;}else{++out.live;out.live_bytes+=bytes;}
  if(c.create_dependencies_quarantined||c.lifecycle_dependencies_quarantined){++out.quarantined;out.quarantined_bytes+=bytes;}};
 for(const auto& c:g_contexts)collect(c,false);for(const auto& c:g_pending_destroy_contexts)collect(c,true);
 nr_compat_present_runtime::AppendTemporalMemory(out);
 LeaveCriticalSection(&g_chain_cs);return out;
}
// Poll only completed exact fences; deltas never borrow the current config identity.
void CollectGpuStats(NrContext& context){
 if(context.temporal)nr_temporal_status::ObserveGpuStats(context.temporal->stats(),
     context.temporal_gpu_observed,g_config_generation.load());
}

struct AdmissionTrace {
 nr_temporal_route::Entry entry=nr_temporal_route::Entry::PostSr;
 unsigned feature=0,requested_backend=0,requested_scope=0,requested_mode=0,requested_legacy=0;
 unsigned actual_backend=0,actual_scope=0,actual_mode=0,actual_validation=0;
 unsigned long long source_stream=0,source_epoch=0,source_sequence=0,owner=0,lifecycle=0;
 nr_temporal_status::Detail detail=nr_temporal_status::Detail::None;
 const char* lower=nullptr;
};
void Status(unsigned value,const AdmissionTrace& trace){
 nr_temporal_status::state.store(value,std::memory_order_relaxed);
 // Prepare/Record production callers hold g_chain_cs. The bitset publication
 // itself is atomic so a diagnostic host cannot race or repeat alternating reasons.
  static nr_temporal_status::AdmissionLogLimiter limiter;
  static nr_temporal_status::DetailLogLimiter details;
  const auto config=g_config_generation.load(std::memory_order_relaxed);
  const bool reason_new=limiter.Observe(config,value);
  const bool detail_new=nr_temporal_status::AdmissionLogLimiter::Actionable(value)&&details.Observe(config,trace.detail);
  if(reason_new||detail_new){
   Log("[temporal-admission] config=%u reason=%s stage=%s lower=%s entry=%u feature=%u source=%llu:%llu:%llu owner=%llu lifecycle=%llu requested=%u:%u:%u:%u actual=%u:%u:%u:%u module=%p build=%s sourceCommit=%s",
      config,nr_temporal_status::Name(value),nr_temporal_status::DetailName(trace.detail),trace.lower?trace.lower:"<none>",
      unsigned(trace.entry),trace.feature,trace.source_stream,trace.source_epoch,trace.source_sequence,
      trace.owner,trace.lifecycle,trace.requested_backend,trace.requested_scope,trace.requested_mode,trace.requested_legacy,
      trace.actual_backend,trace.actual_scope,trace.actual_mode,trace.actual_validation,
      static_cast<void*>(g_addon_module.load(std::memory_order_acquire)),kBuildIdentity,kSourceCommit);
  }
}
bool DropAnchorBindings(NrContext& context){
 if(!context.temporal||!context.temporal->anchor()||!context.binding_pool)return true;
 auto* anchor=context.temporal->anchor();
 const auto references=[anchor](const NrBindingPacket& p){if(!p.valid)return false;
  for(const auto& set:p.plan.heaps)for(unsigned i=0;i<set.count;++i)if(set.views[i].resource==anchor)return true;return false;};
 for(const auto& packet:context.binding_pool->packets)if(references(packet)&&!CanMutateSlotLocked(packet.lease))return false;
 for(auto& packet:context.binding_pool->packets)if(references(packet))DropBindingPacket(packet);
 return true;
}
bool ReleaseIdle(NrContext& context){
 if(!context.temporal)return true;
 // An unrelated, newly recorded NR frame cannot extend the lifetime of the
 // old stabilizer. Its own leases/fences and P1 bindings are authoritative.
 // Keep the Core's hard uncertainty gates; temporal proof cannot repair NGX
 // recording or submission uncertainty in its parent context. Preserve the
 // short Capture-to-Signal transaction until its completion receipt arrives.
 if(g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)||
    context.create_dependencies_quarantined||context.lifecycle_dependencies_quarantined||
    context.awaiting_fence_signal||context.temporal_token||!context.temporal->releasable()||!DropAnchorBindings(context))return false;
 CollectGpuStats(context);
 nr_temporal_status::bytes.fetch_sub(context.temporal->stats().bytes);
 delete context.temporal;context.temporal=nullptr;context.temporal_queue=0;
 context.temporal_retry_tick=0;context.temporal_attempt_generation=0;return true;
}
void Maintain(NrContext& context){
 CollectGpuStats(context);
 if(!context.temporal)return;
 const unsigned requested=g_config.temporal_backend|(g_config.temporal_scope<<8)|(g_config.temporal_mode<<16);
 if(!g_config.enabled||!g_config.temporal_backend||context.retired||
    (context.mode==1?nr_processing_runtime::Effective()!=nr_processing::Start::After:
                     nr_processing_runtime::Effective()!=nr_processing::Start::Before)||
    !context.temporal->initialized()||context.temporal_key!=requested)
  (void)ReleaseIdle(context);
}
struct Prepared {
 RecordedTemporal* runtime=nullptr;
 RecordedInputs inputs{};
 AdmissionTrace trace{};
 ID3D12DescriptorHeap* primary_heap=nullptr;
 unsigned long long recording=0;
 explicit operator bool()const{return runtime!=nullptr;}
};
Prepared PrepareForRoute(nr_temporal_route::Entry entry,ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
 const NrGuideBinding& guides,const FeatureRecord& feature,unsigned width,unsigned height,unsigned x,unsigned y,
 bool native_motion_snapshot=false,const ColorContract* colour=nullptr){
 Prepared out;
 const auto observed=nr_source_runtime::CurrentSource();
 out.trace.entry=entry;out.trace.feature=unsigned(feature.feature_id);
 out.trace.source_stream=observed.stream;out.trace.source_epoch=observed.epoch;out.trace.source_sequence=observed.sequence;
 out.trace.owner=context.instance_id;
 out.trace.requested_backend=g_config.temporal_backend;out.trace.requested_scope=g_config.temporal_scope;
 out.trace.requested_mode=g_config.temporal_mode;out.trace.requested_legacy=g_config.temporal_legacy?1u:0u;
 if(!g_config.temporal_backend){Status(0,out.trace);(void)ReleaseIdle(context);return out;}
 Status(1,out.trace);
 // Native NGX HDR + Automatic float storage is scene-linear, not swapchain PQ.
 // Use the same Evaluate's PreExposure, never a delayed readback/global sample.
 // Strict interior bounds reject values that the legacy reader may have clamped.
 const bool hdr=(feature.create_flags&1)!=0;
 const bool linear_hdr=feature.create_flags_known&&hdr&&slot.codec_color_mode==CodecColorMode::Automatic&&
    colour&&colour->has_pre_exposure&&std::isfinite(colour->pre_exposure)&&
    colour->pre_exposure>1.f/1024.f&&colour->pre_exposure<1024.f&&
    (slot.result_uav_format==DXGI_FORMAT_R16G16B16A16_FLOAT||slot.result_uav_format==DXGI_FORMAT_R11G11B10_FLOAT);
 const auto route=nr_temporal_route::Admit(entry,{unsigned(feature.feature_id),unsigned(context.mode),
    g_config.temporal_backend,g_config.temporal_scope,feature.create_flags_known,(feature.create_flags&1)!=0,
    slot.codec_color_mode==CodecColorMode::Automatic||slot.codec_color_mode==CodecColorMode::SrgbEncodedSdr,
    slot.snapshots_native_output,x==0&&y==0,linear_hdr});
 if(route!=nr_temporal_route::Decision::Supported){
  out.trace.detail=nr_temporal_status::Detail::Route;
  Status(route==nr_temporal_route::Decision::Scope?18:route==nr_temporal_route::Decision::Motion?19:10,out.trace);return out;}
 // The active region starts at (0,0). Ignore allocation padding using typed
 // loads into the existing history; never CopyResource mismatched extents.
 if(!slot.source||!slot.result){out.trace.detail=nr_temporal_status::Detail::MissingCandidate;Status(12,out.trace);return out;}
 const auto source_desc=ID3D12Resource_GetDesc(slot.source);
 const auto target_desc=ID3D12Resource_GetDesc(slot.result);
 if(source_desc.Width<width||source_desc.Height<height||(source_desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&!(linear_hdr&&source_desc.Format==DXGI_FORMAT_R11G11B10_FLOAT))||
    target_desc.Format!=slot.result_uav_format){out.trace.detail=nr_temporal_status::Detail::CandidateExtent;Status(12,out.trace);return out;}
 const auto source=nr_source_runtime::CurrentSource();if(!source)return out;
 if(g_config.temporal_scope&&g_config.nr_passes<2){out.trace.detail=nr_temporal_status::Detail::Anchor;Status(11,out.trace);return out;}
 // Previews deliberately alter candidates and are not a stable colour domain.
 if(g_config.light_preview||g_config.region_preview||g_config.face_preview||g_config.colour_neutral_probe){Status(10,out.trace);return out;}
 CommandListQueueMarker marker{};LifecycleQueue* queue=nullptr;
 if(!ReadLastSubmissionQueueMarker(list,marker)||marker.flags||marker.stable_submissions<kRequiredStableQueueSubmissions){out.trace.detail=nr_temporal_status::Detail::Queue;Status(2,out.trace);return out;}
 for(auto& entry:g_lifecycle_queues)if(entry.identity==marker.identity&&entry.queue&&entry.fence){queue=&entry;break;}
 if(!queue||!ReadLifecycleToken(list,out.recording)||!CanBeginContextUseLocked(list,context,false)){out.trace.detail=nr_temporal_status::Detail::Queue;out.trace.lifecycle=out.recording;Status(2,out.trace);return {};}
 out.trace.lifecycle=out.recording;
 const unsigned key=g_config.temporal_backend|(g_config.temporal_scope<<8)|(g_config.temporal_mode<<16);
 if(context.temporal&&(!context.temporal->initialized()||context.temporal_key!=key||context.temporal_width!=width||context.temporal_height!=height||context.temporal_queue!=queue->identity)){
  if(!ReleaseIdle(context)){out.trace.detail=nr_temporal_status::Detail::Retirement;Status(3,out.trace);return {};}
 }
 auto& in=out.inputs;
 in.settings.enabled=true;in.settings.scope=g_config.temporal_scope?Scope::LaterLayers:Scope::WholeChain;
 // Low-frequency reconstruction requires its neighborhood path; retain the saved legacy choice for ordinary mode.
 in.settings.validation=(g_config.temporal_legacy&&!g_config.temporal_mode)?Validation::Legacy:Validation::Basic065;
 in.settings.mode=g_config.temporal_mode?Mode::LowFrequency:Mode::Adaptive;
 in.layers=g_config.nr_passes==2?(g_config.nr_full_reference?g_config.nr_full_passes:2u):1u;
 const auto backend=g_config.temporal_backend==1?Backend::NativeMotion:Backend::AmdOpticalFlow;
 out.trace.actual_backend=unsigned(backend);out.trace.actual_scope=unsigned(in.settings.scope);
 out.trace.actual_mode=unsigned(in.settings.mode);out.trace.actual_validation=unsigned(in.settings.validation);
 if(backend==Backend::NativeMotion){
  // Independent joint depth/MV snapshot was recorded at the native read boundary.
  // It contains N-pixel vectors already converted with original NGX scales.
  if(!native_motion_snapshot||!slot.temporal_guide_recorded||!slot.temporal_motion||!slot.temporal_depth||
     slot.temporal_snapshot_source.stream!=source.stream||slot.temporal_snapshot_source.epoch!=source.epoch||
     slot.temporal_snapshot_source.sequence!=source.sequence||slot.temporal_snapshot_recording!=out.recording||
     !slot.temporal_width||!slot.temporal_height){out.trace.detail=nr_temporal_status::Detail::NativeSnapshot;Status(17,out.trace);return {};}
  in.native={{source.stream,source.epoch,source.sequence},slot.temporal_motion,DXGI_FORMAT_R32G32_FLOAT,
   slot.temporal_motion_state,0,0,slot.temporal_width,slot.temporal_height,
   float(width)/slot.temporal_width,float(height)/slot.temporal_height,true};
  in.native.depth=slot.temporal_depth;in.native.depth_state=slot.temporal_depth_state;
  in.native.depth_source=in.native.source;in.native.depth_phase_proven=true;
 }
 if(!context.temporal){
  const auto now=GetTickCount64();const auto generation=g_config_generation.load();
   if(context.temporal_attempt_generation==generation&&now<context.temporal_retry_tick){out.trace.detail=nr_temporal_status::Detail::Budget;Status(3,out.trace);return {};}
  context.temporal_attempt_generation=generation;context.temporal_retry_tick=now+2000;
  // Creation/configuration boundary only; no DXGI budget query per frame.
  const auto mem=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(g_codec.device),true);
   if(FAILED(mem.local_result)||mem.local.CurrentUsage>=mem.local.Budget){out.trace.detail=nr_temporal_status::Detail::Budget;Status(3,out.trace);return {};}
  const auto spare=mem.local.Budget-mem.local.CurrentUsage;
  const auto budget=std::min<unsigned long long>(768ull<<20,spare>64ull<<20?spare-(64ull<<20):0);
  context.temporal=new(std::nothrow)RecordedTemporal(g_codec.device,queue->queue,width,height,backend,in.settings.scope,budget);
  if(!context.temporal||!context.temporal->initialized()){
   if(context.temporal)Log("Temporal creation declined: %s; extent=%ux%u backend=%u capBytes=%llu spareBytes=%llu",context.temporal->status(),width,height,unsigned(backend),budget,spare);
    delete context.temporal;context.temporal=nullptr;out.trace.detail=nr_temporal_status::Detail::Budget;Status(3,out.trace);return {};
  }
  context.temporal_key=key;context.temporal_width=width;context.temporal_height=height;context.temporal_queue=queue->identity;
  nr_temporal_status::Allocated(context.temporal->stats().bytes);
 }
 if(!context.temporal->can_record()){out.trace.detail=nr_temporal_status::Detail::Slot;Status(6,out.trace);return {};}
 LARGE_INTEGER clock{},frequency{};QueryPerformanceCounter(&clock);QueryPerformanceFrequency(&frequency);
 const auto time=static_cast<unsigned long long>((static_cast<long double>(clock.QuadPart)*1000000.0L)/frequency.QuadPart);
 in.frame={{source.stream,source.epoch,source.sequence},
  {context.instance_id,(static_cast<unsigned long long>(g_config_generation.load())<<32)|g_history_generation.load(),
   linear_hdr?kResidualLinearScene:kResidualSdrCodeValues,width,height,backend,in.settings.scope,{x,y,width,height,0,context.feature_generation}},time,guides.game_reset!=0};
 in.frame.source_to_scene=linear_hdr?1.f/colour->pre_exposure:1.f;
 nr_temporal_status::domain.store(unsigned(in.frame.contract.color_domain));
 nr_temporal_status::source_to_scene.store(in.frame.source_to_scene);
 in.original=slot.source;in.original_state=slot.source_state;
 in.candidate=slot.result;in.candidate_format=slot.result_uav_format;in.candidate_state=slot.result_state;
 in.candidate_x=x;in.candidate_y=y;
 if(g_config.temporal_scope){
   if(!slot.dual_selected||!context.temporal->anchor()){out.trace.detail=nr_temporal_status::Detail::Anchor;Status(4,out.trace);return {};}
  nr_binding::Plan plan{};plan.primary_owner=&slot;
  WriteCodecDescriptorSet(g_codec.device,slot,nullptr,&plan.heaps[nr_binding::Consumer]);
  auto& views=plan.heaps[nr_binding::Consumer];
  views.Srv(2,slot.neural,DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Srv(10,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);views.Srv(11,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Srv(12,nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
  views.Uav(kCodecResultUavIndex,context.temporal->anchor(),DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Raw(kCodecStatsUavIndex,nullptr,4);views.Raw(kCodecCaptureUavIndex,nullptr,4);
  auto* packet=AcquireBindingPacket(g_codec.device,list,context,plan);
   if(!packet){out.trace.detail=nr_temporal_status::Detail::Anchor;Status(4,out.trace);return {};}
  out.primary_heap=packet->heaps[nr_binding::Consumer];
 }
 out.runtime=context.temporal;return out;
}
// Existing post-SR callers retain their narrow entry. Changing feature_id
// alone never opts one of those callers into RR or pre-SR support.
Prepared Prepare(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
 const NrGuideBinding& guides,const FeatureRecord& feature,unsigned width,unsigned height,unsigned x,unsigned y,
 bool native_motion_snapshot=false){
 return PrepareForRoute(nr_temporal_route::Entry::PostSr,list,context,slot,guides,feature,
     width,height,x,y,native_motion_snapshot);
}
nr_temporal_status::Detail DetailFor(RecordRefusal refusal){
 using D=nr_temporal_status::Detail;
 switch(refusal){
 case RecordRefusal::FrameContract:return D::FrameContract;
 case RecordRefusal::DeviceList:return D::DeviceList;
 case RecordRefusal::OriginalResource:return D::OriginalResource;
 case RecordRefusal::CandidateResource:return D::CandidateResource;
 case RecordRefusal::NativeGuide:return D::NativeGuide;
 case RecordRefusal::TypedCandidate:return D::TypedCandidate;
 case RecordRefusal::Snapshot:return D::Snapshot;
 case RecordRefusal::HistoryPlan:return D::HistoryPlan;
 case RecordRefusal::AmdOwner:return D::AmdOwner;
 case RecordRefusal::AmdBusy:return D::AmdBusy;
 case RecordRefusal::AmdFrame:return D::AmdFrame;
 case RecordRefusal::AmdContinuity:return D::AmdContinuity;
 case RecordRefusal::AmdSnapshot:return D::AmdSnapshot;
 case RecordRefusal::AmdFault:return D::AmdFault;
 default:return D::None;
 }
}
bool Record(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,Prepared& p){
 if(!p)return true;
 p.inputs.original_state=slot.source_state;p.inputs.candidate_state=slot.result_state;
 const auto bytes_before=p.runtime->stats().bytes;
 const auto result=p.runtime->record(list,p.inputs);
 // A format fallback can allocate its snapshot before history/AMD recording.
 // Account the growth on every outcome, including refusal or quarantine;
 // retirement later subtracts the module's actual retained byte count.
 const auto bytes_after=p.runtime->stats().bytes;
 if(bytes_after>bytes_before)nr_temporal_status::Allocated(bytes_after-bytes_before);
 if(result==RecordResult::Recorded){
  if(slot.acceptance_time_active&&slot.acceptance_time_source==nr_source_runtime::CurrentSource())
   slot.acceptance_time_temporal_recorded=true;
   context.temporal_token=p.recording;nr_temporal_status::recorded.fetch_add(1);Status(7,p.trace);return true;
  }
  p.trace.detail=DetailFor(p.runtime->record_refusal());p.trace.lower=p.runtime->status();
  if(result==RecordResult::Faulted){
   context.lifecycle_dependencies_quarantined=true;MarkLifecycleUncertain("temporal opaque recording failure");
   Log("Temporal recording quarantined: %s",p.runtime->status());Status(9,p.trace);return false;
  }
  if(result==RecordResult::Duplicate){Status(13,p.trace);return true;}
  nr_temporal_status::rejected.fetch_add(1);
  Status(result==RecordResult::Busy?6:p.runtime->stats().snapshot_refused?20:12,p.trace);return true;
}
}
