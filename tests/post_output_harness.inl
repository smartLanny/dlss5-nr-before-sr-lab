// Included after the frozen, canonically staged production Core and the
// existing queue fixture. Constructs neural planes; no NVIDIA model is loaded.
#include <vector>
namespace post_output_test {
using queue_test::Check;
using queue_test::Hr;
struct ClearViews {
    ID3D12DescriptorHeap *gpu=nullptr,*cpu=nullptr;
    UINT stride=0;
    explicit ClearViews(ID3D12Device *d) {
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.NumDescriptors=3;
        Hr(ID3D12Device_CreateDescriptorHeap(d,&h,IID_ID3D12DescriptorHeap,(void**)&cpu),"CPU clear heap");
        h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Hr(ID3D12Device_CreateDescriptorHeap(d,&h,IID_ID3D12DescriptorHeap,(void**)&gpu),"GPU clear heap");
        stride=ID3D12Device_GetDescriptorHandleIncrementSize(d,h.Type);
    }
    void Clear(ID3D12Device*d,ID3D12GraphicsCommandList*l,ID3D12Resource*r,unsigned i,const float *v) {
        auto c=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu);c.ptr+=i*stride;
        auto g=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu);g.ptr+=i*stride;
        auto h=ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu);h.ptr+=i*stride;
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=ID3D12Resource_GetDesc(r).Format;desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(d,r,nullptr,&desc,c);ID3D12Device_CreateUnorderedAccessView(d,r,nullptr,&desc,g);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(l,1,&gpu);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(l,h,c,r,v,0,nullptr);UavBarrier(l,r);
    }
    ~ClearViews(){ReleaseCom(gpu);ReleaseCom(cpu);}
};
void Submit(queue_test::Fixture &f,queue_test::List &l) { l.Close();auto *p=l.Base();queue_test::NativeExecute(f.a,1,&p);f.Drain(f.a); }
std::vector<unsigned char> ReadTexture(queue_test::Fixture&f,ID3D12Resource *r,D3D12_RESOURCE_STATES&state) {
    const auto desc=ID3D12Resource_GetDesc(r);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 row_bytes=0,total=0;
    ID3D12Device_GetCopyableFootprints(f.device,&desc,0,1,0,&footprint,&rows,&row_bytes,&total);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *buffer=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&buffer),"readback");
    queue_test::List l(f);const auto old=state;Transition(l.native,r,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=buffer;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(l.native,&dst,0,0,0,&src,nullptr);Transition(l.native,r,state,old);Submit(f,l);
    void *mapped=nullptr;D3D12_RANGE range{0,(SIZE_T)total};Hr(ID3D12Resource_Map(buffer,0,&range,&mapped),"map readback");
    std::vector<unsigned char> result((size_t)row_bytes*rows);
    for(UINT y=0;y<rows;++y)std::memcpy(result.data()+y*row_bytes,(unsigned char*)mapped+footprint.Offset+y*footprint.Footprint.RowPitch,(size_t)row_bytes);
    D3D12_RANGE none{};ID3D12Resource_Unmap(buffer,0,&none);ReleaseCom(buffer);return result;
}
// Per-instance observation only: forward every invocation to the real WARP
// implementation while counting the API operations emitted by DispatchCodec.
struct CodecApiCalls {
    ID3D12GraphicsCommandList *list; ID3D12Resource *target;
    decltype(ID3D12GraphicsCommandList::lpVtbl) original;
    ID3D12GraphicsCommandListVtbl table;
    unsigned dispatches=0, clears=0, copies=0, target_transitions=0, target_uavs=0;
    inline static thread_local CodecApiCalls *active=nullptr;
    CodecApiCalls(ID3D12GraphicsCommandList *l,ID3D12Resource *r):list(l),target(r),original(l->lpVtbl),table(*original) {
        Check(!active,"non-nested native call observer");active=this;
        table.Dispatch=Dispatch;table.ClearUnorderedAccessViewUint=Clear;table.CopyBufferRegion=Copy;table.ResourceBarrier=Barrier;
        list->lpVtbl=&table;
    }
    ~CodecApiCalls(){list->lpVtbl=original;active=nullptr;}
    static void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList*l,UINT x,UINT y,UINT z) {
        auto &a=*active;++a.dispatches;a.original->Dispatch(l,x,y,z);
    }
    static void STDMETHODCALLTYPE Clear(ID3D12GraphicsCommandList*l,D3D12_GPU_DESCRIPTOR_HANDLE g,D3D12_CPU_DESCRIPTOR_HANDLE c,ID3D12Resource*r,const UINT*v,UINT n,const D3D12_RECT*rects) {
        auto &a=*active;++a.clears;a.original->ClearUnorderedAccessViewUint(l,g,c,r,v,n,rects);
    }
    static void STDMETHODCALLTYPE Copy(ID3D12GraphicsCommandList*l,ID3D12Resource*d,UINT64 x,ID3D12Resource*s,UINT64 y,UINT64 n) {
        auto &a=*active;++a.copies;a.original->CopyBufferRegion(l,d,x,s,y,n);
    }
    static void STDMETHODCALLTYPE Barrier(ID3D12GraphicsCommandList*l,UINT n,const D3D12_RESOURCE_BARRIER*b) {
        auto &a=*active;
        for(UINT i=0;i<n;++i) {
            if(b[i].Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b[i].Transition.pResource==a.target)++a.target_transitions;
            if(b[i].Type==D3D12_RESOURCE_BARRIER_TYPE_UAV && b[i].UAV.pResource==a.target)++a.target_uavs;
        }
        a.original->ResourceBarrier(l,n,b);
    }
};
std::vector<unsigned char> Case(queue_test::Fixture &f,DXGI_FORMAT format,bool half,bool identity,bool wrong_target=false,bool paused=false,bool state_only=false) {
    const auto generation=g_config_generation.load();
    const auto epoch=g_pipeline_stats_epoch.load();
    SetNrProbePolicy(state_only?nr_probe_policy::Mode::OutputTransitionsOnly:paused?nr_probe_policy::Mode::Paused:nr_probe_policy::Mode::Normal);
    const bool sampling=!paused&&!state_only;
    Check(g_config_generation.load()==generation && g_pipeline_stats_epoch.load()==epoch,
          "probe switch does not reset configuration or the pipeline");
    g_config=ChainConfig{};g_config.mode=1;g_config.enabled=true;
    g_config.post_transfer_strength=1;g_config.lighting_lock=0;g_config.preserve_lighting=false;
    Check(EnsureCodec(f.device),"real production codec created");
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=16;d.Height=12;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *output=nullptr,*unused_output=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,(void**)&output),"game-owned Output");
    auto &slot=queue_test::context.slots[0];const UINT mw=half?4:8,mh=half?3:6;
    Check(CreateSlot(f.device,output,mw,mh,true,nullptr,slot,CodecColorMode::Automatic,43),"production post slot");
    Check(slot.result==output && !slot.owns_result && slot.snapshots_native_output,"slot aliases real game Output");
    static unsigned long long frame=0;
    slot.diagnostic_identity.frame_id=++frame;
    ClearViews clear(f.device);
    {queue_test::List l(f);const float native[]={.25f,.375f,.5f,1};clear.Clear(f.device,l.native,output,0,native);Submit(f,l);}
    auto before=ReadTexture(f,output,slot.result_state);
    if (wrong_target) {
        // Negative control: computed composition goes to an unused texture.
        // The production probe must still read the actual game Output.
        Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,(void**)&unused_output),"unused output negative control");
        auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        cpu.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*kCodecResultUavIndex;
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(f.device,unused_output,nullptr,&view,cpu);
    }
    slot.delta_stats_capture_armed=true;
    {
        queue_test::List l(f);PostSrSubstitution sub{};sub.slot=&slot;sub.width=8;sub.height=6;sub.work_width=mw;sub.work_height=mh;sub.output_base_x=3;sub.output_base_y=2;sub.native_output_snapshot=true;
        Check(DispatchPostSrNativeSnapshot(l.native,sub),"actual native Output snapshot");
        const float proxy[]={.4f,.4f,.4f,1},neural[]={.65f,.65f,.65f,1};
        Transition(l.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(l.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        clear.Clear(f.device,l.native,slot.proxy,1,proxy);clear.Clear(f.device,l.native,slot.neural,2,identity?proxy:neural);
        Transition(l.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(l.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ColorContract color{};
        const auto actual_probes_before=slot.recovery_probe_count;
        {
            CodecApiCalls calls(l.native,output);
            DispatchCodec(l.native,slot,g_codec.combine,8,6,color,true,0,0,3,2,slot.codec_color_mode,8,6,mw,mh,1,1);
            Check(calls.dispatches==(sampling?2u:1u),"only normal sampling emits a second GPU probe dispatch");
            Check(calls.clears==(sampling?1u:0u) && calls.copies==(sampling?1u:0u),"paused/state-only emits no diagnostic clear or readback copy");
            Check(calls.target_transitions==((sampling||state_only)?2u:0u) &&
                  calls.target_uavs==((sampling||state_only)?1u:0u),"state-only exactly preserves the sampled Output barrier sequence");
        }
        if(state_only) {
            const auto evidence=SnapshotNrOutputTransitionEvidence();
            Check(evidence.phase==NrProbePolicySnapshot().phase && evidence.count>0 && evidence.target==NrFormatResourceIdentity(output),"state-only records its actual target and phase");
            Check(slot.recovery_probe_count==actual_probes_before && !slot.stored_result_metadata.recorded,"state-only cannot create readback recovery proof");
        }
        UavBarrier(l.native,slot.result);Submit(f,l);
    }
    auto after=ReadTexture(f,output,slot.result_state);const size_t pixel_bytes=format==DXGI_FORMAT_R10G10B10A2_UNORM?4:8;
    Check(slot.delta_stats_pending==sampling && slot.post_boundary_probe_recorded==sampling,
          "pause suppresses automatic readback and store probe even if previously armed");
    if(sampling)Check(slot.stored_result_metadata.probe_policy.phase==NrProbePolicySnapshot().phase,
                    "stored result freezes actual recording phase");
    if (format==DXGI_FORMAT_R10G10B10A2_UNORM && sampling) {
        Check(g_codec.post_store_probe!=nullptr,"actual post-store probe compiled");
        D3D12_RANGE range{0,kDiagnosticStatsBytes};void *mapped=nullptr;
        Hr(ID3D12Resource_Map(slot.delta_stats_readback,0,&range,&mapped),"post-store counters map");
        uint32_t v[kDiagnosticStatsUintCount]{};std::memcpy(v,mapped,sizeof(v));
        D3D12_RANGE none{};ID3D12Resource_Unmap(slot.delta_stats_readback,0,&none);
        Check(v[28]==1 && v[39]==1 && v[30]==0,"post-store expected/actual samples paired and finite");
        Check(v[38]==(identity?0u:1u),"expected modification matches model plane");
        Check(v[29]==((identity||wrong_target)?0u:1u),"observed modification checks actual game Output");
        if(wrong_target)Check(v[36]>1000 && v[32]<64,"negative unused-target write distinguished from real storage");
        else Check(std::abs(static_cast<int>(v[32])-static_cast<int>(v[36]))<64,"observed/expected stored deltas agree within typed quantization");
        Check(slot.result_state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"post-store probe restores native Output state");
    }
    if(sampling){
        const auto recording_phase=slot.stored_result_metadata.probe_policy.phase;
        SetNrProbePolicy(nr_probe_policy::Mode::Paused);
        CollectSlotDeltaStatsLocked(queue_test::context,slot);
        Check(!slot.delta_stats_pending,"already submitted readback can retire while sampling is paused");
        const auto sample=SnapshotPostStoreEvidence();
        Check(sample.identity.frame_id==frame && sample.probe_policy.phase==recording_phase &&
              sample.probe_policy.phase!=NrProbePolicySnapshot().phase,
              "late readback retains old phase instead of becoming paused-stage evidence");
    }
    unsigned changed=0;
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        const bool same=std::memcmp(before.data()+(y*16+x)*pixel_bytes,after.data()+(y*16+x)*pixel_bytes,pixel_bytes)==0;
        const bool active=x>=3&&x<11&&y>=2&&y<8;
        if(!active || identity || wrong_target)Check(same,"outside rectangle / identity / unused target retained byte-for-byte");
        else {Check(!same,"changed neural reaches actual game Output");++changed;}
    }
    std::printf("POST_OUTPUT format=%u half=%d identity=%d wrong_target=%d paused=%d state_only=%d changed=%u PASS\n",(unsigned)format,half,identity,wrong_target,paused,state_only,changed);
    DestroySlot(slot);ReleaseCom(output);ReleaseCom(unused_output);
    return after;
}
void Run(){
    queue_test::Fixture f;
    for(auto format:{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT})
        for(bool half:{false,true})for(bool identity:{false,true}) {
            const auto normal=Case(f,format,half,identity);
            const auto paused=Case(f,format,half,identity,false,true);
            const auto state_only=Case(f,format,half,identity,false,false,true);
            Check(normal==paused && normal==state_only,"ordinary, barrier-only and automatic-probe outputs are byte identical");
        }
    Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,false,true);
    Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,false,true,true);
    Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,false,true,false,true);
    SetNrProbePolicy(nr_probe_policy::Mode::Normal);
    std::printf("POST_OUTPUT all checks=%u PASS; constructed neural planes, no NGX model/game\n",queue_test::checks);
}

} // namespace post_output_test
