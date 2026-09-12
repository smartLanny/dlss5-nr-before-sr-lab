// Each auxiliary layer has its own immutable readback and exact submission
// ledger. All layers record their actual result from the same source attempt.
namespace nr_full_capture {
struct Layer {
    nr_replay::Recorder recorder;
    nr_dual_capture::Identity identity{};
    NrSlot* owner=nullptr;
    std::uint64_t token=0,request=0;
    unsigned index=nr_replay::Recorder::Invalid,evaluation=0,since_reset=0;
};
Layer layers[3]{};
void Collect() {
    if(g_lifecycle_tracking_uncertain.load())return;
    for(auto& layer:layers)layer.recorder.Collect(
        [](void* key,std::uint64_t value){auto* queue=static_cast<LifecycleQueue*>(key);
            if(!queue||!queue->fence||!value)return false;
            const auto done=ID3D12Fence_GetCompletedValue(queue->fence);
            return done!=UINT64_MAX&&done>=value;
        },[&](nr_replay::ExportJob* frame){nr_dual_capture::HandoffWithIdentity(frame,layer.identity);});
}
void Begin(ID3D12GraphicsCommandList* list,NrContext& primary,NrSlot& main,
    NrContext& aux,NrSlot& slot,NrSlot& source,const NrGuideBinding& guides,
    unsigned auxiliary,unsigned total_passes) {
    if(auxiliary>=3||total_passes<2||total_passes>4)return;
    auto& layer=layers[auxiliary];const auto request=main.stage_capture.info.request_id;
    if(!main.stage_capture.armed||!request||request==layer.request||
        !layer.recorder.Empty()||nr_dual_capture::workers.load())return;
    nr_replay::Header h{};
    h.sequence=((GetTickCount64()<<16)^GetCurrentProcessId())^(std::uint64_t(auxiliary+1)<<56);
    h.instance=aux.instance_id;h.mode=aux.mode;h.config=g_config_generation.load();
    h.source_width=primary.width;h.source_height=primary.height;
    h.work_width=aux.width;h.work_height=aux.height;h.color_mode=static_cast<unsigned>(aux.codec_color_mode);
    h.source_dirty=kSourceDirty?1:0;std::snprintf(h.source_commit,sizeof(h.source_commit),"%s",kSourceCommit);
    std::snprintf(h.build,sizeof(h.build),"%s",kBuildIdentity);nr_replay::SnapshotScalars(h,aux.params);
    auto* depth=guides.signature.linear_depth?slot.rr_depth:guides.depth;
    auto* motion=guides.signature.resample_motion?slot.motion_proxy:guides.motion;
    nr_replay::Input inputs[nr_replay::kPlanes]={nr_replay::Texture(source.neural,4),
        nr_replay::Texture(slot.proxy,4),nr_replay::Texture(slot.neural,4),
        nr_replay::Texture(depth,1),nr_replay::Texture(motion,2),{},
        nr_replay::Texture(main.result,4,main.result_format)};
    if(!layer.recorder.Ensure(g_codec.device)){nr_dual_capture::status.store(3);return;}
    const auto index=layer.recorder.Arm(h,inputs,IsSameRrDepthDevice);
    if(index==nr_replay::Recorder::Invalid){nr_dual_capture::status.store(3);return;}
    if(!layer.recorder.Begin(list,index,primary.pending_recording_token)) {
        layer.recorder.Discard(index);nr_dual_capture::status.store(3);return;
    }
    layer.index=index;layer.owner=&main;layer.token=primary.pending_recording_token;layer.request=request;
    layer.evaluation=aux.evaluations;layer.since_reset=aux.successful_evaluations_since_reset;
    layer.identity={request,primary.instance_id,aux.instance_id,primary.evaluations,1,1,
        main.dual_model_domain,g_config.generic_residual?1u:0u};
    std::copy_n(g_config.generic_gains,5,layer.identity.gains);
    layer.identity.full_passes=total_passes;layer.identity.layer=auxiliary+2;
    nr_dual_capture::status.store(1);
}
void Finish(ID3D12GraphicsCommandList* list,NrSlot& main) {
    std::uint64_t token=0;if(!ReadLifecycleToken(list,token)||!token)return;
    for(auto& layer:layers) {
        if(layer.index==nr_replay::Recorder::Invalid||layer.owner!=&main||layer.token!=token)continue;
        nr_replay::Header h{};h.evaluation=layer.evaluation;h.since_reset=layer.since_reset;
        h.frame=g_applied_nr_identity.frame_id;
        const auto prior=main.result_state;
        Transition(list,main.result,main.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!layer.recorder.Finish(list,layer.index,h))nr_dual_capture::status.store(3);
        Transition(list,main.result,main.result_state,prior);
        layer.index=nr_replay::Recorder::Invalid;layer.owner=nullptr;layer.token=0;
    }
}
}
namespace nr_dual_capture {
void Submit(std::uint64_t token,void* queue,std::uint64_t fence) {
    recorder.Submit(token,queue,fence);
    for(auto& layer:nr_full_capture::layers)layer.recorder.Submit(token,queue,fence);
}
void Invalidate(std::uint64_t token,bool unsubmitted) {
    recorder.Invalidate(token,unsubmitted);
    for(auto& layer:nr_full_capture::layers)layer.recorder.Invalidate(token,unsubmitted);
}
void Composition(unsigned index,const void* constants,unsigned count,unsigned flags,unsigned pass) {
    recorder.Composition(index,constants,count,flags,pass);
    for(auto& layer:nr_full_capture::layers)
        layer.recorder.Composition(layer.index,constants,count,flags,pass);
}
}
