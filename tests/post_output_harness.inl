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
void Case(queue_test::Fixture &f,DXGI_FORMAT format,bool half,bool identity,bool wrong_target=false) {
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
        DispatchCodec(l.native,slot,g_codec.combine,8,6,color,true,0,0,3,2,slot.codec_color_mode,8,6,mw,mh,1,1);
        UavBarrier(l.native,slot.result);Submit(f,l);
    }
    auto after=ReadTexture(f,output,slot.result_state);const size_t pixel_bytes=format==DXGI_FORMAT_R10G10B10A2_UNORM?4:8;
    if (format==DXGI_FORMAT_R10G10B10A2_UNORM) {
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
    unsigned changed=0;
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        const bool same=std::memcmp(before.data()+(y*16+x)*pixel_bytes,after.data()+(y*16+x)*pixel_bytes,pixel_bytes)==0;
        const bool active=x>=3&&x<11&&y>=2&&y<8;
        if(!active || identity || wrong_target)Check(same,"outside rectangle / identity / unused target retained byte-for-byte");
        else {Check(!same,"changed neural reaches actual game Output");++changed;}
    }
    std::printf("POST_OUTPUT format=%u half=%d identity=%d wrong_target=%d changed=%u PASS\n",(unsigned)format,half,identity,wrong_target,changed);
    DestroySlot(slot);ReleaseCom(output);ReleaseCom(unused_output);
}
void Run(){queue_test::Fixture f;for(auto format:{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT})for(bool half:{false,true})for(bool identity:{false,true})Case(f,format,half,identity);Case(f,DXGI_FORMAT_R10G10B10A2_UNORM,false,false,true);std::printf("POST_OUTPUT all checks=%u PASS; constructed neural planes, no NGX model/game\n",queue_test::checks);}

} // namespace post_output_test
