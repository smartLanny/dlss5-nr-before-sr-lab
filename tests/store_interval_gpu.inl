// Included inside store_quantization_test. Uses the real DispatchCodec,
// production interval writer/probe and CollectSlotDeltaStatsLocked on WARP.
// Only the synthetic input/output producer is a fixture shader; no NR/NGX.
struct IntervalAllocationFailure {
    ID3D12Device *device;decltype(ID3D12Device::lpVtbl) original;ID3D12DeviceVtbl table;
    UINT64 bytes;unsigned denied=0;
    inline static thread_local IntervalAllocationFailure *active=nullptr;
    IntervalAllocationFailure(ID3D12Device *d,UINT64 requested):device(d),original(d->lpVtbl),table(*original),bytes(requested) {
        Check(!active,"non-nested optional stats allocation fault");active=this;
        table.CreateCommittedResource=Create;device->lpVtbl=&table;
    }
    ~IntervalAllocationFailure(){device->lpVtbl=original;active=nullptr;}
    static HRESULT STDMETHODCALLTYPE Create(ID3D12Device *device,const D3D12_HEAP_PROPERTIES *heap,D3D12_HEAP_FLAGS flags,
            const D3D12_RESOURCE_DESC *desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE *clear,REFIID iid,void **out) {
        auto &fault=*active;
        if(desc && desc->Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && desc->Width==fault.bytes &&
           (desc->Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {++fault.denied;*out=nullptr;return E_OUTOFMEMORY;}
        return fault.original->CreateCommittedResource(device,heap,flags,desc,state,clear,iid,out);
    }
};
ID3D12PipelineState *IntervalPipeline(queue_test::Fixture &f,unsigned mode) {
    const auto source=store_interval_fixture::Shader(mode);ID3DBlob *blob=nullptr,*errors=nullptr;
    const auto hr=D3DCompile(source.data(),source.size(),"interval-producer-fixture",nullptr,nullptr,
        "StoreIntervalFixtureCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors)std::printf("%s\n",static_cast<const char *>(errors->lpVtbl->GetBufferPointer(errors)));
    Hr(hr,"compile interval producer with actual diagnostic functions");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=g_codec.root;
    desc.CS={blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob)};
    ID3D12PipelineState *pipeline=nullptr;
    Hr(ID3D12Device_CreateComputePipelineState(f.device,&desc,IID_ID3D12PipelineState,reinterpret_cast<void **>(&pipeline)),
       "create interval fixture pipeline");
    ReleaseCom(blob);ReleaseCom(errors);return pipeline;
}
void IntervalCase(queue_test::Fixture &f,DXGI_FORMAT format,unsigned mode,ID3D12PipelineState *pipeline,
        ID3D12PipelineState *missing_pipeline,bool reuse=false,bool capacity_failure=false,bool bad_reference=false,bool bad_actual=false) {
    constexpr unsigned width=65,height=33,physical_width=78,physical_height=44,samples=6;
    const auto generation=++native_color_test::next_generation;
    native_color_test::ResetContext(true,generation,width,height);
    auto &context=queue_test::context;context.instance_id=generation+9000;context.config_generation=static_cast<unsigned>(generation);
    g_config_generation=context.config_generation;SetNrProbePolicy(nr_probe_policy::Mode::Normal);
    Texture output,expected,poison;
    MakeTexture(f,output,format,format,physical_width,physical_height,std::vector<Pixel>(physical_width*physical_height,{0,0,0,1}),true);
    // 0.5 is exactly the two-code case for both 255 and 1023. The old unique
    // expected count is zero, while every actual coordinate is comparable.
    MakeTexture(f,expected,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
        physical_width,physical_height,std::vector<Pixel>(physical_width*physical_height,{.5f,.5f,.5f,1}),false);
    if(bad_reference || bad_actual)MakeTexture(f,poison,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
        physical_width,physical_height,std::vector<Pixel>(physical_width*physical_height,{Float(0x7fc12345),.5f,.5f,1}),false);
    Observe(f,output,true,generation,format);auto &slot=context.slots[0];
    const auto create=[&] {return CreateSlot(f.device,output.resource,width,height,true,nullptr,slot,
        CodecColorMode::Automatic,format==DXGI_FORMAT_R8G8B8A8_UNORM?0:1,generation);};
    if(capacity_failure) {
        IntervalAllocationFailure fault(f.device,nr_store_comparison::BufferWords(samples)*4);
        Check(create() && fault.denied==1 && slot.store_interval_capacity==0,
              "optional interval allocation failure retains the ordinary NR slot");
    } else Check(create() && slot.store_interval_capacity==samples,"interval capacity follows real Output extent");
    Check(ID3D12Resource_GetDesc(slot.delta_stats).Width==nr_store_comparison::BufferWords(slot.store_interval_capacity)*4 &&
          ID3D12Resource_GetDesc(slot.delta_stats_readback).Width==192,"only aggregate header allocated in readback heap");
    const auto srv=[&](ID3D12DescriptorHeap *heap,unsigned index,ID3D12Resource *resource) {
        auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);cpu.ptr+=index*g_codec.descriptor_size;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(f.device,resource,&view,cpu);
    };
    srv(slot.heap,0,expected.resource);
    srv(slot.snapshot_heap,kCodecEditFieldSrvIndex,bad_reference?poison.resource:expected.resource);
    if(bad_actual)srv(slot.snapshot_heap,0,poison.resource);
    ColorContract color{};
    const auto sample=[&](ID3D12PipelineState *producer,nr_probe_policy::Mode policy) {
        SetNrProbePolicy(policy);context.evaluations=120;ArmDeltaStatsCaptureLocked(slot,context,color);
        ++context.evaluations;CompleteNrDiagnosticEvaluation(context,slot);
        const auto old_frame=SnapshotPostStoreEvidence().frame;const auto probes=slot.recovery_probe_count;
        queue_test::List list(f);const auto original_combine=g_codec.combine;g_codec.combine=producer;
        {
            post_output_test::CodecApiCalls calls(list.native,slot.result,slot.delta_stats);
            DispatchCodec(list.native,slot,g_codec.combine,width,height,color,true,3,2,3,2,
                slot.codec_color_mode,width,height,width,height,1,1);
            const bool normal=policy==nr_probe_policy::Mode::Normal;
            Check(calls.dispatches==(normal?2u:1u) && calls.clears==(normal?1u:0u) && calls.copies==(normal?1u:0u),
                  "interval comparison preserves normal paused state-only dispatch and sampling counts");
            Check(calls.stats_uavs==(normal?3u:0u) && calls.stats_copy_bytes==(normal?192u:0u),
                  "normal interval uses one stats dependency and header-only copy; paused modes do neither");
            Check(calls.target_transitions==((normal||policy==nr_probe_policy::Mode::OutputTransitionsOnly)?2u:0u) &&
                  calls.target_uavs==((normal||policy==nr_probe_policy::Mode::OutputTransitionsOnly)?1u:0u),
                  "interval comparison does not change Output barrier sequence");
        }
        g_codec.combine=original_combine;
        const auto frozen=slot.stored_result_metadata;
        if(policy==nr_probe_policy::Mode::Normal)SetNrProbePolicy(nr_probe_policy::Mode::Paused);
        Submit(f,list);CollectSlotDeltaStatsLocked(context,slot);const auto evidence=SnapshotPostStoreEvidence();
        if(policy==nr_probe_policy::Mode::Normal)
            Check(evidence.frame==frozen.identity.frame_id && evidence.probe_policy.phase==frozen.probe_policy.phase &&
                  evidence.probe_policy.mode==nr_probe_policy::Mode::Normal && evidence.probe_policy.phase!=NrProbePolicySnapshot().phase,
                  "actual interval readback retains original sample phase after pause");
        else Check(evidence.frame==old_frame && slot.recovery_probe_count==probes && !slot.delta_stats_pending,
                   "paused and state-only do not publish new interval recovery evidence");
        return evidence;
    };
    auto evidence=sample(pipeline,nr_probe_policy::Mode::Normal);
    if(capacity_failure)Check(!evidence.comparison_known && (evidence.interval.flags&nr_store_comparison::Capacity),
                             "header-only fallback cannot fabricate an interval match");
    else if(bad_actual)Check(!evidence.comparison_known && evidence.actual_nonfinite && evidence.interval.invalid_actual==samples,
                            "nonfinite actual read cannot pass interval comparison");
    else if(bad_reference)Check(!evidence.comparison_known && evidence.reference_nonfinite,
                               "nonfinite reference remains unknown even with valid stored codes");
    else if(mode<=3 || mode==5) {
        Check(evidence.comparison_known && evidence.matches_expected==(mode<3) && evidence.interval.compared==samples &&
              evidence.interval.mismatched==((mode==3 || mode==5)?samples:0u),
              "both legal endpoints accepted; outside interval and unwritten target rejected");
        Check(evidence.expected_samples==0 && evidence.samples==samples,
              "non-unique legacy expectations do not remove any interval comparison coordinate");
    } else if(mode==4)Check(!evidence.comparison_known && evidence.interval.missing==1 && evidence.interval.written==samples-1,
                            "one omitted interval point remains unknown");
    else if(mode==6)Check(!evidence.comparison_known && evidence.interval.invalid_expected==samples,
                            "nonfinite expected value cannot be legitimized by UNORM clamp");
    else if(mode==7)Check(!evidence.comparison_known && evidence.interval.written==samples+1,
                            "duplicate interval production cannot pass complete-coverage checks");
    else if(mode==8)Check(!evidence.comparison_known && (evidence.interval.flags&nr_store_comparison::MalformedRange),
                            "malformed interval record remains unknown");
    else if(mode==9)Check(!evidence.comparison_known && (evidence.interval.flags&nr_store_comparison::HeaderMismatch),
                            "unrecognized interval header remains unknown");
    if(reuse) {
        evidence=sample(missing_pipeline,nr_probe_policy::Mode::Normal);
        Check(!evidence.comparison_known && evidence.interval.written==samples-1 && evidence.interval.missing==1 &&
              evidence.interval.compared==samples-1,"same-slot full UAV clear removes stale expected point from prior sample");
        sample(pipeline,nr_probe_policy::Mode::Paused);
        sample(pipeline,nr_probe_policy::Mode::OutputTransitionsOnly);
    }
    std::printf("STORE_INTERVAL_WARP format=%u mode=%u reuse=%d capacityFailure=%d badReference=%d badActual=%d PASS\n",
        static_cast<unsigned>(format),mode,reuse,capacity_failure,bad_reference,bad_actual);
    DestroySlot(slot);
}
void IntervalCases(queue_test::Fixture &f) {
    ID3D12PipelineState *pipelines[10]{};for(unsigned mode=0;mode<10;++mode)pipelines[mode]=IntervalPipeline(f,mode);
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM}) {
        for(unsigned mode=0;mode<10;++mode)IntervalCase(f,format,mode,pipelines[mode],pipelines[4],mode==0);
        IntervalCase(f,format,0,pipelines[0],pipelines[4],false,true);
        IntervalCase(f,format,0,pipelines[0],pipelines[4],false,false,true);
        IntervalCase(f,format,0,pipelines[0],pipelines[4],false,false,false,true);
    }
    SetNrProbePolicy(nr_probe_policy::Mode::Normal);
    for(auto &pipeline:pipelines)ReleaseCom(pipeline);
}
