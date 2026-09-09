// Included after the complete staged Core and unchanged WARP fixture helpers.
// Native callback creates real D3D12 descriptors through installed production
// hooks; no ledger is written by this test and no NVIDIA model is loaded.
#include <d3d12sdklayers.h>
namespace native_color_test {
using queue_test::Check;
using queue_test::Hr;
using post_output_test::Submit;
using post_output_test::ReadTexture;
using post_output_test::ClearViews;
unsigned native_calls=0;
unsigned long long next_generation=1000;
struct Params {
    NgxParams base{};
    void *table[17]{};
    ID3D12Resource *color=nullptr,*output=nullptr;
    static NgxResult Get(const NgxParams *base,const char *key,ID3D12Resource **out) {
        const auto *self=reinterpret_cast<const Params*>(base);
        *out=std::strcmp(key,"Color")==0?self->color:std::strcmp(key,"Output")==0?self->output:nullptr;
        return *out?kNgxSuccess:0;
    }
    Params(ID3D12Resource *resource,bool post) {
        base.vtable=table; table[9]=reinterpret_cast<void*>(&Get);
        (post?output:color)=resource;
    }
};
struct Texture {
    ID3D12Resource *resource=nullptr;
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COPY_DEST;
    unsigned width=0,height=0;
    std::vector<unsigned char> bytes;
    Texture()=default;
    Texture(const Texture&)=delete;
    ~Texture(){ReleaseCom(resource);}
};
void MakeTexture(queue_test::Fixture &f,Texture &t,DXGI_FORMAT physical,bool post,
                 unsigned width=16,unsigned height=12,unsigned variant=0) {
    t.width=width;t.height=height;t.bytes.resize(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        auto *p=t.bytes.data()+(y*width+x)*4;
        p[0]=static_cast<unsigned char>(48+(x%7)*9+variant);
        p[1]=static_cast<unsigned char>(88+(y%5)*7);
        p[2]=144; p[3]=static_cast<unsigned char>(37+(x+y)%113);
    }
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=physical;d.Flags=post?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,t.state,
        nullptr,IID_ID3D12Resource,(void**)&t.resource),"native typed/typeless texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource *upload=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,(void**)&upload),"native upload");
    void *mapped=nullptr;D3D12_RANGE none{};
    Hr(ID3D12Resource_Map(upload,0,&none,&mapped),"native upload map");
    for(unsigned y=0;y<height;++y)std::memcpy((unsigned char*)mapped+footprint.Offset+y*footprint.Footprint.RowPitch,
        t.bytes.data()+y*width*4,width*4);
    ID3D12Resource_Unmap(upload,0,nullptr);
    queue_test::List list(f);D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=t.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(list.native,&to,0,0,0,&from,nullptr);
    Transition(list.native,t.resource,t.state,post?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Submit(f,list);ReleaseCom(upload);
}
void Observe(queue_test::Fixture &f,Texture &t,bool post,unsigned long long generation,
             DXGI_FORMAT view=DXGI_FORMAT_R8G8B8A8_UNORM,NgxResult status=kNgxSuccess) {
    queue_test::List list(f);Params params(t.resource,post);NgxHandle handle{};unsigned callback=771;
    NrPrepareFormatObservation(list.native);
    Check(NrFormatGetDevice(f.device).hooks,"production native descriptor hooks published on real WARP device");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;
    ID3D12DescriptorHeap *heap=nullptr;
    Hr(ID3D12Device_CreateDescriptorHeap(f.device,&hd,IID_ID3D12DescriptorHeap,(void**)&heap),"native observed heap");
    const auto destination=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    const unsigned before=native_calls;
    const auto native=[&](ID3D12GraphicsCommandList *l,const NgxHandle *h,const NgxParams *p,void *c) {
        ++native_calls;
        Check(l==list.native&&h==&handle&&p==&params.base&&c==&callback,"native format wrapper preserves all original arguments");
        if(post) {D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=view;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            ID3D12Device_CreateUnorderedAccessView(f.device,t.resource,nullptr,&d,destination);
        } else {D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=view;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
            ID3D12Device_CreateShaderResourceView(f.device,t.resource,&d,destination);}
        return status;
    };
    Check(NrCallNativeWithFormatObservation(native,list.native,&handle,&params.base,&callback,generation)==status&&
          native_calls==before+1,"native callback executes once and returns original status");
    const auto evidence=NrFormatEvidence(NrFormatKey(f.device,t.resource,generation,post));
    Check(evidence.last_call!=0&&evidence.view.format!=0,"actual hook observed native typed descriptor");
    Check(status==kNgxSuccess||evidence.failed,"failed native callback evidence stays rejected");
    list.Close();ReleaseCom(heap);
}
void ResetContext(bool post,unsigned long long generation,unsigned mw=8,unsigned mh=6) {
    for(auto &slot:queue_test::context.slots)DestroySlot(slot);
    queue_test::context.width=mw;queue_test::context.height=mh;
    queue_test::context.feature_generation=generation;queue_test::context.mode=post?1:2;
    queue_test::context.slot_serial=0;queue_test::context.slot_recycles=0;
    g_config=ChainConfig{};g_config.enabled=true;g_config.mode=post?1:2;
    g_config.r8_output_encoding=0;g_config.transfer_strength=1;g_config.post_transfer_strength=1;
    g_config.color_strength=0;g_config.preserve_lighting=false;g_config.lighting_lock=0;
}
float Half(unsigned short h) {
    const unsigned exp=(h>>10)&31u,mant=h&1023u;
    const float value=exp?std::ldexp(1.f+mant/1024.f,(int)exp-15):std::ldexp(mant/1024.f,-14);
    return h&0x8000u?-value:value;
}
float Channel(const std::vector<unsigned char>&bytes,unsigned pixel,unsigned channel) {
    unsigned short value=0;std::memcpy(&value,bytes.data()+pixel*8+channel*2,2);return Half(value);
}
std::vector<unsigned char> Render(queue_test::Fixture &f,Texture &t,NrSlot &slot,
                                   bool post,bool identity,unsigned mw=8,unsigned mh=6) {
    constexpr unsigned aw=8,ah=6,sx=3,sy=2;
    ClearViews clear(f.device);ColorContract color{};
    const auto native_before=ReadTexture(f,t.resource,t.state);
    std::vector<unsigned char> result_before;
    if(!post) {queue_test::List init(f);const float sentinel[]={-2,-2,-2,-2};
        Transition(init.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        clear.Clear(f.device,init.native,slot.result,0,sentinel);Submit(f,init);
        result_before=ReadTexture(f,slot.result,slot.result_state);}
    queue_test::List list(f);
    if(post) {PostSrSubstitution sub{};sub.slot=&slot;sub.width=aw;sub.height=ah;sub.work_width=mw;sub.work_height=mh;
        sub.output_base_x=sx;sub.output_base_y=sy;sub.native_output_snapshot=true;
        Check(DispatchPostSrNativeSnapshot(list.native,sub),"actual packed native snapshot shader");}
    Transition(list.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(list.native,slot,post?g_codec.encode:g_codec.encode_v2,mw,mh,color,false,
        post?0:sx,post?0:sy,0,0,slot.codec_color_mode,aw,ah,mw,mh,1,1);
    UavBarrier(list.native,slot.proxy);
    if(identity) {
        Transition(list.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyResource(list.native,slot.neural,slot.proxy);
    } else {
        Transition(list.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float neural[]={.85f,.65f,.75f,1};clear.Clear(f.device,list.native,slot.neural,1,neural);
    }
    Transition(list.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(list.native,slot,post?g_codec.combine:g_codec.combine_v2,aw,ah,color,false,
        post?0:sx,post?0:sy,post?sx:0,post?sy:0,slot.codec_color_mode,aw,ah,mw,mh,1,1);
    UavBarrier(list.native,slot.result);Submit(f,list);
    auto result=ReadTexture(f,slot.result,slot.result_state);
    t.state=post?slot.result_state:slot.source_state;
    const auto native_after=ReadTexture(f,t.resource,t.state);
    if(!post)Check(native_before==native_after,"pre codec does not alter native Color bytes");
    unsigned changed=0;
    for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
        const bool active=post?(x>=sx&&x<sx+aw&&y>=sy&&y<sy+ah):(x<aw&&y<ah);
        const unsigned pixel=y*t.width+x;
        if(!active) {
            const auto &before=post?native_before:result_before;const unsigned bytes=post?4:8;
            Check(std::memcmp(before.data()+pixel*bytes,result.data()+pixel*bytes,bytes)==0,
                  "outside active rectangle remains byte-identical");
            continue;
        }
        const unsigned original=post?pixel:((y+sy)*t.width+x+sx);
        if(post) {
            Check(result[pixel*4+3]==native_before[original*4+3],"post preserves exact native alpha byte");
            const bool same=std::memcmp(result.data()+pixel*4,native_before.data()+original*4,3)==0;
            if(identity)Check(same,"post identity restores exact native RGB");else changed+=!same;
        } else {
            Check(std::fabs(Channel(result,pixel,3)-native_before[original*4+3]/255.f)<.001f,
                  "pre output retains source alpha within FP16 quantization");
            bool same=true;for(unsigned c=0;c<3;++c)same&=std::fabs(Channel(result,pixel,c)-native_before[original*4+c]/255.f)<.001f;
            if(identity)Check(same,"pre identity restores native values within FP16 quantization");else changed+=!same;
        }
    }
    Check(identity||changed>0,"constructed nonzero NR plane changes active output");
    return result;
}
std::vector<unsigned char> Case(queue_test::Fixture &f,DXGI_FORMAT physical,bool post,bool identity,bool half) {
    const unsigned long long generation=++next_generation;
    const unsigned mw=half?4:8,mh=half?3:6;
    ResetContext(post,generation,mw,mh);Texture original;
    MakeTexture(f,original,physical,post);Observe(f,original,post,generation);
    auto &slot=queue_test::context.slots[0];
    Check(CreateSlot(f.device,original.resource,mw,mh,post,nullptr,slot,
          CodecColorMode::ModelEncodedValueDomain,0,generation),"CREATE_NATIVE_R8_SLOT");
    Check(slot.codec_color_mode==CodecColorMode::ModelEncodedValueDomain,"native R8 uses one bounded value domain without profile override");
    Check((post?slot.result_srv_format:slot.source_srv_format)==DXGI_FORMAT_R8G8B8A8_UNORM,
          "physical and actual typed source view stay distinct");
    Check(FindOrCreateSlot(queue_test::context,f.device,original.resource,post,nullptr,
          CodecColorMode::ModelEncodedValueDomain,0)==&slot,"FindOrCreateSlot reuses exact same native view identity");
    WriteCodecDescriptorSet(f.device,slot,slot.heap);
    if(post)WritePostSrSnapshotDescriptors(f.device,slot);
    auto rendered=Render(f,original,slot,post,identity,mw,mh);
    // Actual same-format resource rotation, including a different physical
    // extent, exercises production result/snapshot reallocation and writers.
    Texture replacement;MakeTexture(f,replacement,physical,post,20,15,3);
    Observe(f,replacement,post,generation);
    Check(RebindSlotExternal(f.device,replacement.resource,post,nullptr,slot,
        CodecColorMode::ModelEncodedValueDomain,0,generation),"production native-view RebindSlotExternal");
    Check((post?slot.result:slot.source)==replacement.resource,"rebind publishes correct external resource");
    Render(f,replacement,slot,post,identity,mw,mh);
    for(auto &s:queue_test::context.slots)DestroySlot(s);
    std::printf("NATIVE_COLOR physical=%u post=%d identity=%d half=%d PASS\n",(unsigned)physical,post,identity,half);
    return rendered;
}
struct SlotIdentity {
    ID3D12Resource *key,*source,*result,*proxy,*neural;
    ID3D12DescriptorHeap *heap,*snapshot;
    DXGI_FORMAT source_view,result_view,result_uav;
    nr_native_format::Key native_key;
    explicit SlotIdentity(const NrSlot&s):key(s.key),source(s.source),result(s.result),proxy(s.proxy),neural(s.neural),
        heap(s.heap),snapshot(s.snapshot_heap),source_view(s.source_srv_format),result_view(s.result_srv_format),result_uav(s.result_uav_format),native_key(s.native_format_key){}
    bool Same(const NrSlot&s) const {return key==s.key&&source==s.source&&result==s.result&&proxy==s.proxy&&neural==s.neural&&
        heap==s.heap&&snapshot==s.snapshot_heap&&source_view==s.source_srv_format&&result_view==s.result_srv_format&&
        result_uav==s.result_uav_format&&native_key==s.native_format_key;}
};
void RejectionAndReuse(queue_test::Fixture&f,bool post) {
    const auto generation=++next_generation;ResetContext(post,generation);
    std::vector<std::unique_ptr<Texture>> resources;
    for(unsigned i=0;i<9;++i) {
        auto t=std::make_unique<Texture>();MakeTexture(f,*t,DXGI_FORMAT_R8G8B8A8_TYPELESS,post,16,12,i);
        Observe(f,*t,post,generation);
        Check(FindOrCreateSlot(queue_test::context,f.device,t->resource,post,nullptr,
              CodecColorMode::ModelEncodedValueDomain,0)!=nullptr,"native view rotating slot allocation/reuse");
        resources.push_back(std::move(t));
    }
    Check(queue_test::context.slot_recycles==1,"ninth retired resource uses actual FindOrCreateSlot recycle path");
    auto *slot=FindOrCreateSlot(queue_test::context,f.device,resources.back()->resource,post,nullptr,
        CodecColorMode::ModelEncodedValueDomain,0);Check(slot!=nullptr,"last rotating slot present");
    Texture unknown;MakeTexture(f,unknown,DXGI_FORMAT_R8G8B8A8_TYPELESS,post);
    const auto before=SlotIdentity(*slot);const auto calls=native_calls;
    Check(!RebindSlotExternal(f.device,unknown.resource,post,nullptr,*slot,CodecColorMode::ModelEncodedValueDomain,0,generation),
          "unknown view refuses rebind");
    Check(!FindOrCreateSlot(queue_test::context,f.device,unknown.resource,post,nullptr,CodecColorMode::ModelEncodedValueDomain,0),
          "unknown view refuses cached slot lookup");
    Check(before.Same(*slot)&&native_calls==calls,"unknown gate neither partially rebinds nor adds native SR calls");
    Render(f,*resources.back(),*slot,post,true);
    // Unsupported interpretation without a prior UNORM observation is a
    // separate rejection from contradictory observations of one object.
    Texture unsupported;MakeTexture(f,unsupported,DXGI_FORMAT_R8G8B8A8_TYPELESS,post);
    Observe(f,unsupported,post,generation,post?DXGI_FORMAT_R8G8B8A8_UINT:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    static NrSlot rejected;
    const auto unsupported_calls=native_calls;const auto stable=SlotIdentity(*slot);
    Check(!CreateSlot(f.device,unsupported.resource,8,6,post,nullptr,rejected,
          CodecColorMode::ModelEncodedValueDomain,0,generation),"unsupported-only native typed view refuses CreateSlot");
    Check(!rejected.key&&!rejected.source&&!rejected.result&&!rejected.heap,
          "rejected CreateSlot publishes no partial resource dependencies");
    Check(!RebindSlotExternal(f.device,unsupported.resource,post,nullptr,*slot,
          CodecColorMode::ModelEncodedValueDomain,0,generation),"unsupported-only view refuses rebind");
    Check(stable.Same(*slot)&&native_calls==unsupported_calls,
          "unsupported-only rejection preserves prior slot and native callback count");
    Render(f,*resources.back(),*slot,post,true);
    // Actual legal alternate typed descriptor establishes conflicting evidence.
    Observe(f,*resources.back(),post,generation,post?DXGI_FORMAT_R8G8B8A8_UINT:DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    const auto conflicting=SlotIdentity(*slot);const auto conflict_calls=native_calls;
    Check(!FindOrCreateSlot(queue_test::context,f.device,resources.back()->resource,post,nullptr,CodecColorMode::ModelEncodedValueDomain,0),
          "conflict revokes existing slot lookup");
    Check(!RebindSlotExternal(f.device,resources.back()->resource,post,nullptr,*slot,CodecColorMode::ModelEncodedValueDomain,0,generation),
          "conflict refuses rebind");
    Check(conflicting.Same(*slot)&&native_calls==conflict_calls,"conflict is fail-closed without native calls or partial writes");
    // A new Feature generation requires its own observation even at the same
    // resource address. A successful new view cannot reuse an old slot key.
    queue_test::context.feature_generation=generation+100;
    Observe(f,*resources.back(),post,generation+100);
    Check(!FindOrCreateSlot(queue_test::context,f.device,resources.back()->resource,post,nullptr,CodecColorMode::ModelEncodedValueDomain,0),
          "Feature generation change does not inherit cached slot key");
    for(auto &s:queue_test::context.slots)DestroySlot(s);
}
void Run() {
    ID3D12Debug *debug=nullptr;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,(void**)&debug)))ID3D12Debug_EnableDebugLayer(debug);
    queue_test::Fixture fixture;
    Check(EnsureCodec(fixture.device),"actual complete production codec compilation");
    for(bool post:{false,true})for(bool identity:{false,true})for(bool half:{false,true}) {
        auto typed=Case(fixture,DXGI_FORMAT_R8G8B8A8_UNORM,post,identity,half);
        auto typeless=Case(fixture,DXGI_FORMAT_R8G8B8A8_TYPELESS,post,identity,half);
        Check(typed==typeless,"typed28 and native-observed typeless27 have identical final storage");
    }
    RejectionAndReuse(fixture,false);RejectionAndReuse(fixture,true);
    ID3D12InfoQueue *messages=nullptr;
    if(SUCCEEDED(ID3D12Device_QueryInterface(fixture.device,IID_ID3D12InfoQueue,(void**)&messages))) {
        for(UINT64 i=0;i<ID3D12InfoQueue_GetNumStoredMessages(messages);++i) {
            SIZE_T bytes=0;ID3D12InfoQueue_GetMessage(messages,i,nullptr,&bytes);std::vector<unsigned char> storage(bytes);
            auto *m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Hr(ID3D12InfoQueue_GetMessage(messages,i,m,&bytes),"D3D12 debug message");
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)std::printf("D3D12_ERROR %s\n",m->pDescription);
            Check(m->Severity>D3D12_MESSAGE_SEVERITY_ERROR,"no D3D12 debug layer error");
        }
    }
    std::printf("NATIVE_COLOR debug_layer=%d info_queue=%d\n",debug!=nullptr,messages!=nullptr);
    ReleaseCom(messages);ReleaseCom(debug);
    std::printf("NATIVE_COLOR PASS checks=%u native_callbacks=%u; WARP-only full production Core, constructed neural planes, no NGX/game\n",
        queue_test::checks,native_calls);
}
} // namespace native_color_test
