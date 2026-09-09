// Complete production Core plus the native-color WARP helpers precede this.
// The probe reads the owned Result after its store. Test-only neural planes
// and deliberately redirected writes never stand in for measured storage.
namespace pre_store_test {
using queue_test::Check;using queue_test::Hr;
using native_color_test::Texture;
using native_color_test::MakeTexture;
using native_color_test::Observe;
using post_output_test::ReadTexture;
using post_output_test::Submit;
using post_output_test::ClearViews;
void MakeSource(queue_test::Fixture &f,Texture &t,DXGI_FORMAT physical) {
    if(physical!=DXGI_FORMAT_R32G32B32A32_FLOAT) {MakeTexture(f,t,physical,false);return;}
    t.width=16;t.height=12;t.bytes.resize(t.width*t.height*16);
    for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
        const float pixel[]={123.456f+(x%7)*3.14f,511.13f+(y%5)*.37f,1024.3333f,.625f};
        std::memcpy(t.bytes.data()+(y*t.width+x)*16,pixel,sizeof(pixel));
    }
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=t.width;d.Height=t.height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=physical;d.Flags=D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,t.state,
        nullptr,IID_ID3D12Resource,(void**)&t.resource),"FP32 HDR original without UAV permission");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource *upload=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload),"FP32 HDR upload");
    void *mapped=nullptr;D3D12_RANGE none{};
    Hr(ID3D12Resource_Map(upload,0,&none,&mapped),"FP32 HDR upload map");
    for(unsigned y=0;y<t.height;++y)std::memcpy((unsigned char*)mapped+footprint.Offset+y*footprint.Footprint.RowPitch,
        t.bytes.data()+y*t.width*16,t.width*16);
    ID3D12Resource_Unmap(upload,0,nullptr);
    queue_test::List list(f);D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=t.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(list.native,&to,0,0,0,&from,nullptr);
    Transition(list.native,t.resource,t.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Submit(f,list);ReleaseCom(upload);
}
void Case(queue_test::Fixture &f,DXGI_FORMAT physical,bool half,bool identity,bool wrong_target) {
    constexpr unsigned width=8,height=6,source_x=3,source_y=2;
    const auto generation=++native_color_test::next_generation;
    const unsigned mw=half?4:width,mh=half?3:height;
    native_color_test::ResetContext(false,generation,mw,mh);
    auto &context=queue_test::context;
    context.evaluations=0;context.instance_id=generation+7000;
    context.config_generation=static_cast<unsigned>(generation);
    g_config_generation=context.config_generation;
    const bool hdr=physical==DXGI_FORMAT_R32G32B32A32_FLOAT;
    const auto view=hdr?physical:DXGI_FORMAT_R8G8B8A8_UNORM;
    const auto mode=hdr?CodecColorMode::Automatic:CodecColorMode::ModelEncodedValueDomain;
    Texture source;MakeSource(f,source,physical);
    Check(ID3D12Resource_GetDesc(source.resource).Flags==D3D12_RESOURCE_FLAG_NONE,
          "actual original Color has no UAV permission");
    Observe(f,source,false,generation,view);
    auto *slot=FindOrCreateSlot(context,f.device,source.resource,false,nullptr,
        mode,0);
    Check(slot&&slot->pre_store_probe_heap,"real pre slot owns optional stored-result probe descriptors");
    Check(slot->result_format==DXGI_FORMAT_R16G16B16A16_FLOAT&&slot->owns_result,
          "pre evidence target is the actual owned FP16 Result");
    const auto original_bytes=ReadTexture(f,source.resource,source.state);
    ClearViews clear(f.device);ColorContract color{};
    if(hdr) {
        // Explicit scene exposure keeps the constructed nonidentity model edit
        // measurable above FP16 spacing at this high original magnitude.
        color.source_to_canonical=1.f/1024.f;color.pre_exposure=1024.f;
        color.has_pre_exposure=true;
    }
    // Establish an actual unchanged reference in Result before the negative
    // target substitution, using the production CopyCS and typed source view.
    {queue_test::List init(f);
        Transition(init.native,slot->result,slot->result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float sentinel[]={-2,-2,-2,-2};clear.Clear(f.device,init.native,slot->result,0,sentinel);
        DispatchCodec(init.native,*slot,g_codec.copy,width,height,color,hdr,source_x,source_y,0,0,
            slot->codec_color_mode,width,height,mw,mh,1,2);
        UavBarrier(init.native,slot->result);Submit(f,init);}
    const auto reference=ReadTexture(f,slot->result,slot->result_state);
    ID3D12Resource *unused=nullptr;
    if(wrong_target) {
        Check(CreateTexture(f.device,source.width,source.height,L"ProbeFixture.UnusedResult",&unused),"unused real Result target");
        auto destination=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot->heap);
        destination.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*kCodecResultUavIndex;
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(f.device,unused,nullptr,&view,destination);
    }
    ArmDeltaStatsCaptureLocked(*slot,context,color);
    Check(slot->delta_stats_capture_armed,"existing first-three-frames schedule arms sample");
    ++context.evaluations;CompleteNrDiagnosticEvaluation(context,*slot);
    const auto frozen_identity=slot->diagnostic_identity;
    const auto frozen_key=slot->native_format_key;
    const auto prior_evidence=SnapshotPreStoreEvidence();
    ID3D12Fence *gate=f.Fence();
    queue_test::List list(f);
    Check(RecordNrUseLocked(list.native,context,*slot),"actual recording/retirement ownership accepted");
    Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(list.native,*slot,g_codec.encode_v2,mw,mh,color,hdr,source_x,source_y,0,0,
        slot->codec_color_mode,width,height,mw,mh,1,2);
    UavBarrier(list.native,slot->proxy);
    if(identity) {
        Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyResource(list.native,slot->neural,slot->proxy);
    } else {
        Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float edited[]={.85f,.65f,.75f,1};clear.Clear(f.device,list.native,slot->neural,1,edited);
    }
    Transition(list.native,slot->proxy,slot->proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list.native,slot->neural,slot->neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(unused) {auto state=D3D12_RESOURCE_STATE_COMMON;Transition(list.native,unused,state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    DispatchCodec(list.native,*slot,g_codec.combine_v2,width,height,color,hdr,source_x,source_y,0,0,
        slot->codec_color_mode,width,height,mw,mh,1,2);
    Check(slot->result_state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS&&
          slot->source_state==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          "probe restores established target/source resource states");
    Check(slot->stored_result_metadata.recorded&&slot->stored_result_metadata.mode==2&&
          !slot->post_boundary_probe_recorded,"pre recording is separate from post boundary evidence");
    Hr(ID3D12CommandQueue_Wait(f.a,gate,1),"hold real GPU completion before collector");
    list.Close();list.Submit(f.a);
    CollectSlotDeltaStatsLocked(context,*slot);
    Check(slot->delta_stats_pending&&SnapshotPreStoreEvidence().frame==prior_evidence.frame,
          "pending actual GPU fence cannot publish stored-result evidence");
    Hr(ID3D12Fence_Signal(gate,1),"release fixture dependency");f.Drain(f.a);
    CollectSlotDeltaStatsLocked(context,*slot);
    Check(slot->delta_stats_pending,"completed GPU but retained recording still cannot publish");
    list.Detach();
    const auto actual=ReadTexture(f,slot->result,slot->result_state);
    Check(ReadTexture(f,source.resource,source.state)==original_bytes,"probe never writes original no-UAV Color");
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x) {
        const unsigned pixel=y*source.width+x;
        if(x>=width||y>=height||identity||wrong_target)
            Check(std::memcmp(actual.data()+pixel*8,reference.data()+pixel*8,8)==0,
                  "outside, identity and unused-target Result bytes are retained");
    }
    // Rotate the slot after actual GPU completion but before collection. The
    // readback must retain the old typed view and anonymous resource lifetime.
    Texture next;const auto next_format=hdr?physical:(physical==DXGI_FORMAT_R8G8B8A8_TYPELESS?
        DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R8G8B8A8_TYPELESS);
    MakeSource(f,next,next_format);Observe(f,next,false,generation,view);
    WriteCodecDescriptorSet(f.device,*slot,slot->heap);
    Check(RebindSlotExternal(f.device,next.resource,false,nullptr,*slot,
        mode,0,generation),"retired real slot can rotate before readback collection");
    if(hdr)Check(slot->native_format_key==frozen_key&&frozen_key==nr_native_format::Key{},
          "direct typed FP32 retains the production unknown observer key");
    else Check(!(slot->native_format_key==frozen_key),"fixture rotated to a different actual resource lifetime");
    CollectSlotDeltaStatsLocked(context,*slot);
    const auto evidence=SnapshotPreStoreEvidence();
    Check(!slot->delta_stats_pending&&evidence.frame==frozen_identity.frame_id&&evidence.mode==2,
          "completed readback publishes exact recorded front frame");
    Check(evidence.identity.instance_id==frozen_identity.instance_id&&evidence.config==frozen_identity.config_generation&&
          evidence.feature_generation==generation&&evidence.native_key==frozen_key,
          "stored identity/config/Feature/resource belongs to recording, not rebound slot");
    Check(evidence.source_physical==static_cast<unsigned>(physical)&&evidence.source_view==static_cast<unsigned>(view)&&
          evidence.result_physical==10&&evidence.result_view==10,"stored physical/view metadata frozen before rotation");
    Check(evidence.samples==1&&evidence.expected_samples==1&&evidence.invalid==0,
          "actual and pre-store sparse samples are paired and finite");
    if(!hdr)Check(evidence.expected_changed==(identity?0u:1u)&&evidence.changed==((identity||wrong_target)?0u:1u),
          "expected and actual R8 changes distinguish identity, changed and wrong target");
    if(wrong_target)Check(std::fabs(evidence.expected_mean-evidence.actual_mean)>.01f,
          "WRONG_PRE_TARGET: expected stored delta disagrees with actual Result");
    else Check(std::fabs(evidence.expected_mean-evidence.actual_mean)<.00002f&&
          std::fabs(evidence.expected_max-evidence.actual_max)<.000002f,
          "expected and actual Result deltas agree after diagnostic FP16 conversion");
    if(hdr&&identity)Check(evidence.changed==1&&evidence.expected_changed==1&&
          evidence.expected_mean>.1f&&evidence.actual_mean>.1f,
          "HDR identity precision loss is stored quantization, not an NR effect");
    const unsigned evaluations[]={0,1,2,3,119,120,121};
    for(auto count:evaluations) {
        slot->delta_stats_capture_armed=false;context.evaluations=count;
        ArmDeltaStatsCaptureLocked(*slot,context,color);
        Check(slot->delta_stats_capture_armed==(count<3||count%120==0),"unchanged first-three/120 sparse sampling cadence");
    }
    for(auto&s:context.slots)DestroySlot(s);
    ReleaseCom(gate);ReleaseCom(unused);
    std::printf("PRE_STORE physical=%u half=%d identity=%d wrong_target=%d expected=%.6f actual=%.6f PASS\n",
        (unsigned)physical,half,identity,wrong_target,evidence.expected_mean,evidence.actual_mean);
}
void Run() {
    queue_test::Fixture f;Check(EnsureCodec(f.device)&&g_codec.post_store_probe,"actual stored-result probe compiled");
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_TYPELESS})
        for(bool half:{false,true})for(bool identity:{false,true})Case(f,format,half,identity,false);
    Case(f,DXGI_FORMAT_R8G8B8A8_TYPELESS,true,false,true);
    Case(f,DXGI_FORMAT_R32G32B32A32_FLOAT,false,true,false);
    Case(f,DXGI_FORMAT_R32G32B32A32_FLOAT,true,true,false);
    Case(f,DXGI_FORMAT_R32G32B32A32_FLOAT,true,false,false);
    Case(f,DXGI_FORMAT_R32G32B32A32_FLOAT,true,false,true);
    // Existing post-storage shader/restore contract remains exercised, without
    // adding the pre reference offset flag to the post dispatch.
    post_output_test::Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,true);
    post_output_test::Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,true,false);
    post_output_test::Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,false,true);
    std::printf("PRE_STORE WARP PASS checks=%u; actual Core/stats/fences/readback; constructed neural planes, no NGX/game\n",queue_test::checks);
}
} // namespace pre_store_test
