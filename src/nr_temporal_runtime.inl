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

void Status(unsigned value){nr_temporal_status::state.store(value,std::memory_order_relaxed);}
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
 ID3D12DescriptorHeap* primary_heap=nullptr;
 unsigned long long recording=0;
 explicit operator bool()const{return runtime!=nullptr;}
};
Prepared PrepareForRoute(nr_temporal_route::Entry entry,ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
 const NrGuideBinding& guides,const FeatureRecord& feature,unsigned width,unsigned height,unsigned x,unsigned y,
 bool native_motion_snapshot=false,const ColorContract* colour=nullptr){
 Prepared out;
 if(!g_config.temporal_backend){Status(0);(void)ReleaseIdle(context);return out;}
 Status(1);
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
  Status(route==nr_temporal_route::Decision::Scope?18:route==nr_temporal_route::Decision::Motion?19:10);return out;}
 // The active region starts at (0,0). Ignore allocation padding using typed
 // loads into the existing history; never CopyResource mismatched extents.
 if(!slot.source||!slot.result){Status(12);return out;}
 const auto source_desc=ID3D12Resource_GetDesc(slot.source);
 const auto target_desc=ID3D12Resource_GetDesc(slot.result);
 if(source_desc.Width<width||source_desc.Height<height||(source_desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&!(linear_hdr&&source_desc.Format==DXGI_FORMAT_R11G11B10_FLOAT))||
    target_desc.Format!=slot.result_uav_format){Status(12);return out;}
 const auto source=nr_source_runtime::CurrentSource();if(!source)return out;
 if(g_config.temporal_scope&&g_config.nr_passes<2){Status(11);return out;}
 // Previews deliberately alter candidates and are not a stable colour domain.
 if(g_config.light_preview||g_config.region_preview||g_config.face_preview||g_config.colour_neutral_probe){Status(10);return out;}
 CommandListQueueMarker marker{};LifecycleQueue* queue=nullptr;
 if(!ReadLastSubmissionQueueMarker(list,marker)||marker.flags||marker.stable_submissions<kRequiredStableQueueSubmissions){Status(2);return out;}
 for(auto& entry:g_lifecycle_queues)if(entry.identity==marker.identity&&entry.queue&&entry.fence){queue=&entry;break;}
 if(!queue||!ReadLifecycleToken(list,out.recording)||!CanBeginContextUseLocked(list,context,false)){Status(2);return {};}
 const unsigned key=g_config.temporal_backend|(g_config.temporal_scope<<8)|(g_config.temporal_mode<<16);
 if(context.temporal&&(!context.temporal->initialized()||context.temporal_key!=key||context.temporal_width!=width||context.temporal_height!=height||context.temporal_queue!=queue->identity)){
  if(!ReleaseIdle(context)){Status(3);return {};}
 }
 auto& in=out.inputs;
 in.settings.enabled=true;in.settings.scope=g_config.temporal_scope?Scope::LaterLayers:Scope::WholeChain;
 // Low-frequency reconstruction requires its neighborhood path; retain the saved legacy choice for ordinary mode.
 in.settings.validation=(g_config.temporal_legacy&&!g_config.temporal_mode)?Validation::Legacy:Validation::Basic065;
 in.settings.mode=g_config.temporal_mode?Mode::LowFrequency:Mode::Adaptive;
 in.layers=g_config.nr_passes==2?(g_config.nr_full_reference?g_config.nr_full_passes:2u):1u;
 const auto backend=g_config.temporal_backend==1?Backend::NativeMotion:Backend::AmdOpticalFlow;
 if(backend==Backend::NativeMotion){
  // Independent joint depth/MV snapshot was recorded at the native read boundary.
  // It contains N-pixel vectors already converted with original NGX scales.
  if(!native_motion_snapshot||!slot.temporal_guide_recorded||!slot.temporal_motion||!slot.temporal_depth||
     slot.temporal_snapshot_source.stream!=source.stream||slot.temporal_snapshot_source.epoch!=source.epoch||
     slot.temporal_snapshot_source.sequence!=source.sequence||slot.temporal_snapshot_recording!=out.recording||
     !slot.temporal_width||!slot.temporal_height){Status(17);return {};}
  in.native={{source.stream,source.epoch,source.sequence},slot.temporal_motion,DXGI_FORMAT_R32G32_FLOAT,
   slot.temporal_motion_state,0,0,slot.temporal_width,slot.temporal_height,
   float(width)/slot.temporal_width,float(height)/slot.temporal_height,true};
  in.native.depth=slot.temporal_depth;in.native.depth_state=slot.temporal_depth_state;
  in.native.depth_source=in.native.source;in.native.depth_phase_proven=true;
 }
 if(!context.temporal){
  const auto now=GetTickCount64();const auto generation=g_config_generation.load();
  if(context.temporal_attempt_generation==generation&&now<context.temporal_retry_tick){Status(3);return {};}
  context.temporal_attempt_generation=generation;context.temporal_retry_tick=now+2000;
  // Creation/configuration boundary only; no DXGI budget query per frame.
  const auto mem=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(g_codec.device),true);
  if(FAILED(mem.local_result)||mem.local.CurrentUsage>=mem.local.Budget){Status(3);return {};}
  const auto spare=mem.local.Budget-mem.local.CurrentUsage;
  const auto budget=std::min<unsigned long long>(768ull<<20,spare>64ull<<20?spare-(64ull<<20):0);
  context.temporal=new(std::nothrow)RecordedTemporal(g_codec.device,queue->queue,width,height,backend,in.settings.scope,budget);
  if(!context.temporal||!context.temporal->initialized()){
   if(context.temporal)Log("Temporal creation declined: %s",context.temporal->status());
   delete context.temporal;context.temporal=nullptr;Status(3);return {};
  }
  context.temporal_key=key;context.temporal_width=width;context.temporal_height=height;context.temporal_queue=queue->identity;
  nr_temporal_status::Allocated(context.temporal->stats().bytes);
 }
 if(!context.temporal->can_record()){Status(6);return {};}
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
  if(!slot.dual_selected||!context.temporal->anchor()){Status(4);return {};}
  nr_binding::Plan plan{};plan.primary_owner=&slot;
  WriteCodecDescriptorSet(g_codec.device,slot,nullptr,&plan.heaps[nr_binding::Consumer]);
  auto& views=plan.heaps[nr_binding::Consumer];
  views.Srv(2,slot.neural,DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Srv(10,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);views.Srv(11,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Srv(12,nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT);
  views.Uav(kCodecResultUavIndex,context.temporal->anchor(),DXGI_FORMAT_R16G16B16A16_FLOAT);
  views.Raw(kCodecStatsUavIndex,nullptr,4);views.Raw(kCodecCaptureUavIndex,nullptr,4);
  auto* packet=AcquireBindingPacket(g_codec.device,list,context,plan);
  if(!packet){Status(4);return {};}
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
  context.temporal_token=p.recording;nr_temporal_status::recorded.fetch_add(1);Status(7);return true;
 }
 if(result==RecordResult::Faulted){
  context.lifecycle_dependencies_quarantined=true;MarkLifecycleUncertain("temporal opaque recording failure");
  Log("Temporal recording quarantined: %s",p.runtime->status());Status(9);return false;
 }
 if(result==RecordResult::Duplicate){Status(13);return true;}
 nr_temporal_status::rejected.fetch_add(1);
 Status(result==RecordResult::Busy?6:p.runtime->stats().snapshot_refused?20:12);return true;
}
}
