// Included after the complete Core and existing WARP resource helpers.
// The numerical oracle writes real typed UAVs; no NVIDIA model is loaded.
#include <bit>
#include <limits>
#include "store_interval_shader.h"
namespace store_quantization_test {
using queue_test::Check;using queue_test::Hr;
using native_color_test::Texture;using native_color_test::Observe;
using post_output_test::ReadTexture;using post_output_test::Submit;
using post_output_test::ClearViews;
struct Pixel {float r,g,b,a;};
struct OraclePixel {Pixel expected;unsigned lower,upper;};
static_assert(sizeof(OraclePixel)==24);
unsigned oracle_samples=0,oracle_unknown=0;
unsigned Bits(float f){return std::bit_cast<unsigned>(f);}
float Float(unsigned b){return std::bit_cast<float>(b);}
float SmallValue(unsigned code,unsigned fraction_bits,bool signed_value=false) {
    const unsigned sign=signed_value?(code>>(fraction_bits+5)):0;
    const unsigned exponent=(code>>fraction_bits)&31, fraction=code&((1u<<fraction_bits)-1);
    float value=exponent==31?(fraction?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()):
        exponent?std::ldexp(1.f+static_cast<float>(fraction)/(1u<<fraction_bits),static_cast<int>(exponent)-15):
        std::ldexp(static_cast<float>(fraction),-14-static_cast<int>(fraction_bits));
    return sign?-value:value;
}
unsigned SmallBits(float value,unsigned fraction_bits,bool signed_value=false) {
    const unsigned bits=Bits(value),exponent=(bits>>23)&255,fraction=bits&0x7fffff;
    const unsigned sign=signed_value?(bits>>31)<<(fraction_bits+5):0, infinity=31u<<fraction_bits;
    if(exponent==255) {
        if(fraction)return sign|infinity|1;
        return !signed_value&&(bits>>31)?0:sign|infinity;
    }
    if(!signed_value&&(bits>>31))return 0;
    if(exponent>=143)return sign|(infinity-1);
    if(exponent<113) {
        const unsigned shift=136-fraction_bits-exponent;
        return sign|(shift>=32?0:((1u<<23)|fraction)>>shift);
    }
    return sign|((exponent-112)<<fraction_bits)|(fraction>>(23-fraction_bits));
}
unsigned BytesPerPixel(DXGI_FORMAT f) {return f==DXGI_FORMAT_R32G32B32A32_FLOAT?16:f==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;}
std::vector<unsigned char> Encode(const std::vector<Pixel>&values,DXGI_FORMAT view) {
    const auto stride=BytesPerPixel(view);std::vector<unsigned char> bytes(values.size()*stride);
    for(size_t i=0;i<values.size();++i) {
        const auto *v=&values[i].r;auto *out=bytes.data()+i*stride;
        if(view==DXGI_FORMAT_R32G32B32A32_FLOAT)std::memcpy(out,v,16);
        else if(view==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            for(unsigned c=0;c<4;++c){const auto h=static_cast<unsigned short>(SmallBits(v[c],10,true));std::memcpy(out+c*2,&h,2);}
        } else if(view==DXGI_FORMAT_R11G11B10_FLOAT) {
            const unsigned p=SmallBits(v[0],6)|(SmallBits(v[1],6)<<11)|(SmallBits(v[2],5)<<22);std::memcpy(out,&p,4);
        } else {
            const unsigned n=view==DXGI_FORMAT_R8G8B8A8_UNORM?255:1023;
            unsigned q[4]{};for(unsigned c=0;c<4;++c)q[c]=static_cast<unsigned>(std::floor(std::clamp(v[c],0.f,1.f)*(c==3&&n==1023?3:n)+.5));
            if(n==255)for(unsigned c=0;c<4;++c)out[c]=static_cast<unsigned char>(q[c]);
            else {const unsigned p=q[0]|(q[1]<<10)|(q[2]<<20)|(q[3]<<30);std::memcpy(out,&p,4);}
        }
    }
    return bytes;
}
void MakeTexture(queue_test::Fixture&f,Texture&t,DXGI_FORMAT physical,DXGI_FORMAT view,
                 unsigned width,unsigned height,const std::vector<Pixel>&pixels,bool uav) {
    t.width=width;t.height=height;t.bytes=Encode(pixels,view);
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=physical;
    d.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,t.state,nullptr,
        IID_ID3D12Resource,(void**)&t.resource),"oracle texture allocation");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    ID3D12Device_GetCopyableFootprints(f.device,&d,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=total;
    b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ID3D12Resource *upload=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,IID_ID3D12Resource,(void**)&upload),"oracle texture upload");
    void *mapped=nullptr;D3D12_RANGE none{};Hr(ID3D12Resource_Map(upload,0,&none,&mapped),"oracle upload map");
    const auto row=width*BytesPerPixel(view);
    for(unsigned y=0;y<height;++y)std::memcpy((unsigned char*)mapped+footprint.Offset+y*footprint.Footprint.RowPitch,t.bytes.data()+y*row,row);
    ID3D12Resource_Unmap(upload,0,nullptr);queue_test::List list(f);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    to.pResource=t.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(list.native,&to,0,0,0,&from,nullptr);
    Transition(list.native,t.resource,t.state,uav?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Submit(f,list);ReleaseCom(upload);
}
ID3D12Resource *MakeBuffer(queue_test::Fixture&f,UINT64 bytes,bool readback) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
    d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags=readback?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=readback?D3D12_HEAP_TYPE_READBACK:D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource *r=nullptr;Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&d,
        readback?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,
        IID_ID3D12Resource,(void**)&r),"oracle buffer allocation");return r;
}
ID3D12PipelineState *OraclePipeline(queue_test::Fixture&f) {
    const std::string source=std::string(kCodecShader)+R"HLSL(
[numthreads(8,8,1)] void StoreQuantizationFixtureCS(uint3 tid:SV_DispatchThreadID) {
    uint2 coord=tid.xy*32u;if(coord.x>=Width||coord.y>=Height)return;
    float4 value=OriginalColor.Load(int3(coord,0));
    ResultOut[coord]=value;
    bool known;float3 expected=StoreExpectedValue(value.rgb,known);
    uint i=(coord.y/32u)*((Width+31u)/32u)+coord.x/32u;
    StageCapture.Store4(i*24u,asuint(float4(expected,known?1.0:0.0)));
    uint2 bounds=uint2(0u,0u);uint kind=(Passthrough>>20u)&7u;
    if((kind==2u || kind==3u) && isfinite(value.x))bounds=StoreUnormBounds(value.x,kind==2u?255u:1023u);
    StageCapture.Store2(i*24u+16u,bounds);
    RecordPostStoreExpected(coord,value.rgb);
}
)HLSL";
    ID3DBlob *blob=nullptr,*errors=nullptr;
    const HRESULT hr=D3DCompile(source.data(),source.size(),"actual-store-oracle",nullptr,nullptr,
        "StoreQuantizationFixtureCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
    if(FAILED(hr)&&errors)std::printf("%s\n",(const char*)errors->lpVtbl->GetBufferPointer(errors));
    Hr(hr,"compile real storage oracle with production diagnostic functions");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=g_codec.root;
    desc.CS={blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob)};
    ID3D12PipelineState *p=nullptr;Hr(ID3D12Device_CreateComputePipelineState(f.device,&desc,IID_ID3D12PipelineState,(void**)&p),"oracle pipeline");
    ReleaseCom(blob);ReleaseCom(errors);return p;
}
unsigned StoredCode(const std::vector<unsigned char>&bytes,unsigned pixel,DXGI_FORMAT format,unsigned channel) {
    if(format==DXGI_FORMAT_R16G16B16A16_FLOAT) {unsigned short v=0;std::memcpy(&v,bytes.data()+pixel*8+channel*2,2);return v;}
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM)return bytes[pixel*4+channel];
    unsigned v=0;std::memcpy(&v,bytes.data()+pixel*4,4);
    if(format==DXGI_FORMAT_R10G10B10A2_UNORM)return (v>>(channel*10))&1023;
    return channel==2?v>>22:(v>>(channel*11))&2047;
}
std::vector<float> Values(DXGI_FORMAT format) {
    std::vector<float> values={0.f,-0.f,1.f,-1.f,1e-30f,-1e-30f,65504.f,65536.f,70000.f,-70000.f,
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),Float(0x7fc12345),Float(0xffc12345)};
    auto around=[&](float lo,float hi){values.push_back(lo);values.push_back(std::nextafter(lo,hi));
        values.push_back(lo+(hi-lo)*.5f);values.push_back(std::nextafter(hi,lo));};
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R10G10B10A2_UNORM) {
        const unsigned n=format==DXGI_FORMAT_R8G8B8A8_UNORM?255:1023;
        for(unsigned i=0;i<n;++i) {
            values.push_back(static_cast<float>(i)/n);
            for(double fraction:{.39,.4,.41,.49,.5,.51,.59,.6,.61}) {
                const auto value=static_cast<float>((i+fraction)/n);values.push_back(value);
                if(fraction==.4 || fraction==.6) {
                    values.push_back(std::nextafter(value,-std::numeric_limits<float>::infinity()));
                    values.push_back(std::nextafter(value,std::numeric_limits<float>::infinity()));
                }
            }
        }
        values.push_back(std::nextafter(1.f,0.f));values.push_back(std::nextafter(1.f,2.f));
    } else if(format==DXGI_FORMAT_R11G11B10_FLOAT) {
        for(unsigned m:{6u,5u})for(unsigned code=0;code<(31u<<m)-1;++code)around(SmallValue(code,m),SmallValue(code+1,m));
        for(unsigned m:{6u,5u})around(SmallValue((31u<<m)-1,m),65536.f);
    } else {
        for(unsigned exponent=0;exponent<31;++exponent)for(unsigned fraction:{0u,1u,511u,512u,1022u}) {
            const unsigned code=(exponent<<10)|fraction;const float lo=SmallValue(code,10),hi=SmallValue(code+1,10);
            around(lo,hi);around(-hi,-lo);
        }
    }
    return values;
}
void OracleBatch(queue_test::Fixture&f,ID3D12PipelineState*pipeline,DXGI_FORMAT format,
                 const std::vector<float>&values,size_t first,DXGI_FORMAT physical=DXGI_FORMAT_UNKNOWN) {
    constexpr unsigned columns=16,rows=16,width=columns*32,height=rows*32,count=columns*rows;
    std::vector<Pixel> pixels(width*height,Pixel{0,0,0,1});
    for(unsigned i=0;i<count;++i) {
        const float v=first+i<values.size()?values[first+i]:0;
        pixels[((i/columns)*32)*width+(i%columns)*32]={v,v,v,1};
    }
    Texture input;MakeTexture(f,input,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,width,height,pixels,false);
    static NrSlot slot;Check(CreateSlot(f.device,input.resource,1,1,false,nullptr,slot),"real descriptor/stat allocation for typed-store oracle");
    ReleaseCom(slot.result);D3D12_RESOURCE_DESC target=ID3D12Resource_GetDesc(input.resource);
    target.Format=physical==DXGI_FORMAT_UNKNOWN?format:physical;target.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,IID_ID3D12Resource,(void**)&slot.result),"actual typed UAV oracle target");
    slot.result_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;slot.result_format=target.Format;
    slot.result_srv_format=slot.result_uav_format=format;
    WriteCodecDescriptorSet(f.device,slot,slot.heap);WritePreStoreProbeDescriptors(f.device,slot);
    ID3D12Resource *capture=MakeBuffer(f,count*sizeof(OraclePixel),false),*capture_readback=MakeBuffer(f,count*sizeof(OraclePixel),true);
    BindStageCaptureDescriptor(slot,capture,count*sizeof(OraclePixel));
    CodecConstants constants{};constants.width=width;constants.height=height;
    constants.passthrough=131072u|8388608u|(NrStoreDiagnosticFormat(format)<<20);
    queue_test::List list(f);ID3D12GraphicsCommandList_SetComputeRootSignature(list.native,g_codec.root);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list.native,1,&slot.heap);
    auto gpu=ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.heap);
    const auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.stats_clear_heap);
    auto stats_gpu=gpu;stats_gpu.ptr+=static_cast<UINT64>(g_codec.descriptor_size)*kCodecStatsUavIndex;
    Transition(list.native,slot.delta_stats,slot.delta_stats_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT zero[4]{};ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list.native,stats_gpu,cpu,slot.delta_stats,zero,0,nullptr);
    UavBarrier(list.native,slot.delta_stats);ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list.native,0,gpu);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list.native,1,kCodecRootConstantCount,&constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list.native,pipeline);ID3D12GraphicsCommandList_Dispatch(list.native,(columns+7)/8,(rows+7)/8,1);
    UavBarrier(list.native,slot.delta_stats);
    UavBarrier(list.native,slot.result);Transition(list.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list.native,1,&slot.pre_store_probe_heap);
    gpu=ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.pre_store_probe_heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list.native,0,gpu);
    ID3D12GraphicsCommandList_SetPipelineState(list.native,g_codec.post_store_probe);
    ID3D12GraphicsCommandList_Dispatch(list.native,(columns+7)/8,(rows+7)/8,1);
    Transition(list.native,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    UavBarrier(list.native,capture);auto capture_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Transition(list.native,capture,capture_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(list.native,capture_readback,0,capture,0,count*sizeof(OraclePixel));Submit(f,list);
    const auto actual=ReadTexture(f,slot.result,slot.result_state);
    OraclePixel expected[count]{};void *mapped=nullptr;D3D12_RANGE range{0,sizeof(expected)};
    Hr(ID3D12Resource_Map(capture_readback,0,&range,&mapped),"oracle diagnostic results");std::memcpy(expected,mapped,sizeof(expected));
    D3D12_RANGE none{};ID3D12Resource_Unmap(capture_readback,0,&none);
    for(unsigned i=0;i<count&&first+i<values.size();++i) {
        const float v=values[first+i];const unsigned pixel=((i/columns)*32)*width+(i%columns)*32;
        bool unique=std::isfinite(v);const auto *q=&expected[i].expected.r;
        if(format==DXGI_FORMAT_R32G32B32A32_FLOAT) {
            unique=false;
            for(unsigned c=0;c<3;++c) {
                float stored=0;std::memcpy(&stored,actual.data()+pixel*16+c*4,4);
                Check(std::isnan(v)?std::isnan(stored)&&std::isnan(q[c]):Bits(stored)==Bits(v)&&Bits(q[c])==Bits(v),
                    "unsupported diagnostic format still writes and reads real FP32 storage");
            }
        } else if(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R10G10B10A2_UNORM) {
            const unsigned maximum=format==DXGI_FORMAT_R8G8B8A8_UNORM?255:1023;
            const double bounded=std::isnan(v)?0:std::clamp(static_cast<double>(v),0.,1.);
            const double scaled=bounded*maximum,fraction=scaled-std::floor(scaled);
            unique&=fraction<.4||fraction>.6;
            if(std::isfinite(v)) {
                // Independent binary64 oracle: an FP32 value times <=1023
                // is exact here. Test 5*distance<=3, never an epsilon band.
                unsigned low=maximum,high=0,accepted=0;
                const int base=static_cast<int>(std::floor(scaled));
                for(int candidate=std::max(0,base-1);candidate<=std::min(static_cast<int>(maximum),base+2);++candidate)
                    if(std::abs(static_cast<double>(candidate)-scaled)*5.0<=3.0) {
                        low=std::min(low,static_cast<unsigned>(candidate));high=std::max(high,static_cast<unsigned>(candidate));++accepted;
                    }
                Check(accepted>=1 && accepted<=2 && expected[i].lower==low && expected[i].upper==high,
                      "production UNORM interval equals independent exact legal-code set");
                for(unsigned c=0;c<3;++c)Check(StoredCode(actual,pixel,format,c)>=low && StoredCode(actual,pixel,format,c)<=high,
                      "actual typed store is within the exact allowed interval");
            }
            for(unsigned c=0;c<3;++c) {
                const auto code=StoredCode(actual,pixel,format,c),ideal=static_cast<unsigned>(std::floor(scaled+.5));
                Check(std::abs(static_cast<double>(code)-scaled)<=.60000000001,"actual UNORM encoding satisfies specified 0.6 integer ULP");
                Check(static_cast<unsigned>(std::lround(q[c]*maximum))==ideal,"diagnostic UNORM ideal conversion agrees with independent exact-double oracle");
                if(unique)Check(code==ideal,"unique UNORM expectation equals actual storage bits");
            }
        } else for(unsigned c=0;c<3;++c) {
            const unsigned m=format==DXGI_FORMAT_R16G16B16A16_FLOAT?10:c==2?5:6;
            const bool signed_value=format==DXGI_FORMAT_R16G16B16A16_FLOAT;
            const auto actual_code=StoredCode(actual,pixel,format,c),ideal=SmallBits(v,m,signed_value);
            const float ideal_value=SmallValue(ideal,m,signed_value);
            if(std::isnan(v)) {
                Check(std::isnan(SmallValue(actual_code,m,signed_value))&&std::isnan(q[c]),"NaN remains nonfinite in actual and diagnostic floating storage");
            } else {
                if(actual_code!=ideal||Bits(q[c])!=Bits(ideal_value))
                    std::printf("ORACLE_MISMATCH fmt=%u input=%08x channel=%u actual=%04x ideal=%04x expected=%08x value=%08x\n",(unsigned)format,Bits(v),c,actual_code,ideal,Bits(q[c]),Bits(ideal_value));
                Check(actual_code==ideal,"actual narrow float storage equals direct FP32 truncation bits");
                Check(Bits(q[c])==Bits(ideal_value),"diagnostic narrow float equals independent exact encoding oracle");
            }
            unique&=std::isfinite(ideal_value);
        }
        Check((expected[i].expected.a==1.f)==unique,"legacy unique expectation still reports ambiguous format conversion");
        ++oracle_samples;oracle_unknown+=!unique;
    }
    DestroySlot(slot);ReleaseCom(capture);ReleaseCom(capture_readback);
}
void FullCoreCase(queue_test::Fixture&f,DXGI_FORMAT format,bool half,bool identity,bool wrong_target,bool high=false,unsigned nonfinite_case=0,bool light=false,bool packet=false) {
    const auto generation=++native_color_test::next_generation;
    const unsigned mw=half?4:8,mh=half?3:6;
    native_color_test::ResetContext(true,generation,mw,mh);
    if(light){g_config.light_broad=1.25f;g_config.light_dark=.75f;g_config.light_control_mode=1;}
    auto& context=queue_test::context;context.instance_id=generation+9000;context.evaluations=0;
    context.config_generation=static_cast<unsigned>(generation);g_config_generation=context.config_generation;
    Pixel original=high?Pixel{128.75f,511.25f,1024.3333f,.625f}:Pixel{.25f,.375f,.5f,1};
    const bool invalid=nonfinite_case!=0;
    if(nonfinite_case==1)original.r=std::numeric_limits<float>::quiet_NaN();
    Texture output;MakeTexture(f,output,format,format,16,12,std::vector<Pixel>(16*12,original),true);
    Observe(f,output,true,generation,format);
    auto& slot=context.slots[0];
    const int create_flags=format==DXGI_FORMAT_R8G8B8A8_UNORM?0:1;
    Check(CreateSlot(f.device,output.resource,mw,mh,true,nullptr,slot,CodecColorMode::Automatic,create_flags,generation),"actual post Core slot for storage comparison");
    Check(slot.snapshot_heap&&slot.result==output.resource,"actual post target and probe heap exist, including FP16");
    const auto before=ReadTexture(f,output.resource,slot.result_state);
    ID3D12Resource *unused=nullptr;
    if(wrong_target) {
        const auto desc=ID3D12Resource_GetDesc(output.resource);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,IID_ID3D12Resource,(void**)&unused),"same typed format unused-output negative control");
        auto destination=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        destination.ptr+=static_cast<SIZE_T>(g_codec.descriptor_size)*kCodecResultUavIndex;
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(f.device,unused,nullptr,&view,destination);
    }
    ColorContract color{};if(high){color.pre_exposure=1024;color.source_to_canonical=1.f/1024;color.has_pre_exposure=true;}
    ArmDeltaStatsCaptureLocked(slot,context,color);++context.evaluations;CompleteNrDiagnosticEvaluation(context,slot);
    const auto identity_at_recording=slot.diagnostic_identity;
    queue_test::List list(f);Check(RecordNrUseLocked(list.native,context,slot),"real post store recording ownership");
    if(packet) {
        nr_binding::Plan plan{};plan.primary_owner=&slot;
        WriteCodecDescriptorSet(f.device,slot,nullptr,&plan.heaps[nr_binding::Consumer]);
        slot.compose_binding=AcquireBindingPacket(f.device,list.native,context,plan);
        Check(slot.compose_binding&&slot.compose_binding->valid,"actual immutable packet is materialized for stored-result sampling");
        Check(plan.heaps[nr_binding::Consumer].views[kCodecStatsUavIndex].elements==nr_store_comparison::BufferWords(slot.store_interval_capacity),
              "immutable diagnostic UAV covers header and per-coordinate intervals");
    }
    PostSrSubstitution sub{};sub.slot=&slot;sub.width=8;sub.height=6;sub.work_width=mw;sub.work_height=mh;
    sub.output_base_x=3;sub.output_base_y=2;sub.native_output_snapshot=true;
    Check(DispatchPostSrNativeSnapshot(list.native,sub),"actual typed native Output snapshot");
    ClearViews clear(f.device);const float proxy[]={.4f,.4f,.4f,1},neural[]={.65f,.65f,.65f,1};
    if(nonfinite_case==2||nonfinite_case==3) {
        Check(wrong_target,"isolated nonfinite fixture redirects only the Combine write");
        const float poison[]={std::numeric_limits<float>::quiet_NaN(),.375f,.5f,1};
        if(nonfinite_case==2)clear.Clear(f.device,list.native,slot.result,0,poison);
        else {
            Transition(list.native,slot.source,slot.source_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            clear.Clear(f.device,list.native,slot.source,0,poison);
            Transition(list.native,slot.source,slot.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }
    Transition(list.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    clear.Clear(f.device,list.native,slot.proxy,1,proxy);clear.Clear(f.device,list.native,slot.neural,2,identity?proxy:neural);
    Transition(list.native,slot.proxy,slot.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list.native,slot.neural,slot.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DispatchCodec(list.native,slot,g_codec.combine,8,6,color,true,0,0,3,2,slot.codec_color_mode,8,6,mw,mh,1,1);
    Check(slot.stored_result_metadata.result_uav==static_cast<unsigned>(format),"comparison format frozen from actual Result UAV view");
    Check(slot.result_state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"post diagnostic restores real target state");
    ID3D12Fence *gate=f.Fence();Hr(ID3D12CommandQueue_Wait(f.a,gate,1),"hold post store fence");
    const auto old=SnapshotPostStoreEvidence();list.Close();list.Submit(f.a);CollectSlotDeltaStatsLocked(context,slot);
    Check(slot.delta_stats_pending&&SnapshotPostStoreEvidence().frame==old.frame,"comparison unavailable before real completion fence");
    Hr(ID3D12Fence_Signal(gate,1),"release post store fence");f.Drain(f.a);CollectSlotDeltaStatsLocked(context,slot);
    Check(slot.delta_stats_pending,"comparison unavailable while recording reference retained");list.Detach();
    CollectSlotDeltaStatsLocked(context,slot);const auto evidence=SnapshotPostStoreEvidence();
    Check(!slot.delta_stats_pending&&evidence.frame==identity_at_recording.frame_id&&evidence.mode==1&&
        evidence.result_uav==static_cast<unsigned>(format),"completed actual storage comparison has exact frame and UAV format");
    if(invalid) {
        Check(!evidence.comparison_known&&!evidence.matches_expected&&evidence.invalid==1,
            "nonfinite probe publishes fresh unknown with masked union count");
        Check(evidence.actual_nonfinite==(nonfinite_case!=3)&&evidence.reference_nonfinite==(nonfinite_case!=2),
            "actual-only and reference-only nonfinite are separate measured evidence");
        Check(evidence.expected_samples==(nonfinite_case==2?1u:0u),"nonfinite reference/expectation does not fabricate known pairs");
    }
    else Check(evidence.comparison_known,"finite supported storage has a complete format-aware comparison");
    if(!invalid && nr_store_comparison::Unorm(NrStoreDiagnosticFormat(format)))
        Check(evidence.comparison_method==1 && evidence.interval.written==1 && evidence.interval.compared==1 &&
              evidence.interval.flags==0 && evidence.interval.mismatched==(wrong_target?1u:0u),
              "full production UNORM pipeline compares every planned coordinate");
    if(evidence.comparison_known)Check(evidence.matches_expected==!wrong_target,"exact stored fingerprint/counters distinguish real write from wrong target");
    else Check(!evidence.matches_expected,"unknown precision never reports a successful comparison");
    const auto after=ReadTexture(f,output.resource,slot.result_state);const auto stride=BytesPerPixel(format);
    unsigned changed=0;
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        const bool active=x>=3&&x<11&&y>=2&&y<8;
        const bool same=std::memcmp(before.data()+(y*16+x)*stride,after.data()+(y*16+x)*stride,stride)==0;
        if((!active&&nonfinite_case!=2)||(!invalid&&(identity||wrong_target)))Check(same,"actual target outside/identity/unused bytes retained");
        else changed+=!same;
    }
    if(!identity&&!wrong_target&&!invalid)Check(changed!=0,"constructed edited model plane reaches actual typed Output");
    std::printf("STORE_CORE format=%u half=%d identity=%d wrong=%d high=%d nonfinite_case=%u light=%d packet=%d actual_nonfinite=%d reference_nonfinite=%d known=%d match=%d expected=%.6f actual=%.6f PASS\n",
        (unsigned)format,half,identity,wrong_target,high,nonfinite_case,light,packet,evidence.actual_nonfinite,evidence.reference_nonfinite,
        evidence.comparison_known,evidence.matches_expected,evidence.expected_mean,evidence.actual_mean);
    DestroyBindings(context);
    DestroySlot(slot);ReleaseCom(gate);ReleaseCom(unused);
}
#include "store_interval_gpu.inl"
void Run(bool oracle=true) {
    queue_test::Fixture f;Check(EnsureCodec(f.device)&&g_codec.post_store_probe,"production diagnostic shaders compile");
    ID3D12PipelineState *pipeline=OraclePipeline(f);
    if(oracle)for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT}) {
        const auto values=Values(format);const auto unknown_before=oracle_unknown;
        for(size_t first=0;first<values.size();first+=256)OracleBatch(f,pipeline,format,values,first);
        std::printf("STORE_ORACLE format=%u samples=%u unknown=%u PASS\n",(unsigned)format,(unsigned)values.size(),oracle_unknown-unknown_before);
    }
    const std::vector<float> unknown_values={0.f,.25f,1.f,123.456f,-12.25f,std::numeric_limits<float>::infinity(),Float(0x7fc12345)};
    Check(NrStoreDiagnosticFormat(DXGI_FORMAT_R32G32B32A32_FLOAT)==0,"unproven diagnostic format is explicitly unknown");
    OracleBatch(f,pipeline,DXGI_FORMAT_R32G32B32A32_FLOAT,unknown_values,0);
    const std::vector<float> typed_values={0.f,.25f,.5f,.12345f,.7f,1.f};
    OracleBatch(f,pipeline,DXGI_FORMAT_R8G8B8A8_UNORM,typed_values,0,DXGI_FORMAT_R8G8B8A8_TYPELESS);
    std::printf("STORE_ORACLE unknown FP32 and physical R8 typeless / typed UNORM UAV PASS\n");
    ReleaseCom(pipeline);
    for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT}) {
        FullCoreCase(f,format,false,true,false);FullCoreCase(f,format,true,false,false);FullCoreCase(f,format,true,false,true);
    }
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,false,true,false,true);
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,true,false,false,true);
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,true,false,true,true);
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,false,true,false,false,true);
    FullCoreCase(f,DXGI_FORMAT_R16G16B16A16_FLOAT,false,true,false,false,true);
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,true,false,true,false,2);
    FullCoreCase(f,DXGI_FORMAT_R11G11B10_FLOAT,true,false,true,false,3);
    pre_store_test::Case(f,DXGI_FORMAT_R8G8B8A8_TYPELESS,true,false,false);
    pre_store_test::Case(f,DXGI_FORMAT_R8G8B8A8_TYPELESS,true,false,true);
    pre_store_test::Case(f,DXGI_FORMAT_R32G32B32A32_FLOAT,true,true,false);
    // D14 overlap regression: light controls and storage diagnostics must use
    // independent bits, including the immutable descriptor path from D13.
    for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R11G11B10_FLOAT})
        for(bool packet:{false,true})FullCoreCase(f,format,true,false,false,false,0,true,packet);
    pre_store_test::Case(f,DXGI_FORMAT_R8G8B8A8_TYPELESS,true,false,false,true);
    IntervalCases(f);
    std::printf("STORE_QUANTIZATION WARP PASS checks=%u samples=%u unknown=%u; actual typed UAV storage, no NGX/game\n",queue_test::checks,oracle_samples,oracle_unknown);
}
} // namespace store_quantization_test
