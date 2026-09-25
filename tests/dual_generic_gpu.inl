// Real production spatial helper and composition shaders, synthetic NR output.
struct DualEval {
    Gpu* gpu;nr_dual::dx12::Surfaces surfaces;unsigned calls=0;bool changed=false,fail=false;
};
std::uint32_t SyntheticSecond(void* user,ID3D12GraphicsCommandList*,const void*,const void*) {
    auto& e=*static_cast<DualEval*>(user);++e.calls;if(e.fail)return 0xBAD00005u;
    auto& g=*e.gpu;
    if(e.changed) {
        g.list->SetDescriptorHeaps(1,&g.heap.p);
        g.UavTexture(0,e.surfaces.output,DXGI_FORMAT_R16G16B16A16_FLOAT);
        const float values[4]={.5f,.25f,.75f,1.f};
        g.list->ClearUnorderedAccessViewFloat(g.heap->GetGPUDescriptorHandleForHeapStart(),g.Cpu(0),e.surfaces.output,values,0,nullptr);
    } else {
        g.Barrier(e.surfaces.input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        g.Barrier(e.surfaces.output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        g.list->CopyResource(e.surfaces.output,e.surfaces.input);
        g.Barrier(e.surfaces.input,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.Barrier(e.surfaces.output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    return 1;
}
bool CompletedDual(void* user) {
    auto& g=*static_cast<Gpu*>(user);
    return g.submitted&&g.fence->GetCompletedValue()!=UINT64_MAX&&g.fence->GetCompletedValue()>=g.submitted;
}
Texture BorrowReadback(Gpu& g,ID3D12Resource* resource) {
    Texture t;t.resource.p=resource;resource->AddRef();auto d=resource->GetDesc();
    t.width=static_cast<unsigned>(d.Width);t.height=d.Height;t.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    UINT64 bytes=0;g.device->GetCopyableFootprints(&d,0,1,0,&t.footprint,nullptr,nullptr,&bytes);
    t.readback=g.Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    return t;
}
void ValidateDualSpatial(Gpu& g) {
    using namespace nr_dual;unsigned cases=0;
    const std::string probe_source=std::string(kCodecShader)+"\n[numthreads(8,8,1)] void DualResolveProbe(uint3 p:SV_DispatchThreadID){if(p.x<ModelWidth&&p.y<ModelHeight)ResultOut[p.xy]=NRDualFinalStored(int2(p.xy),uint2(ModelWidth,ModelHeight),DualLowSize,DualModelDomain);}";
    Com<ID3DBlob> code,errors;const auto compiled=D3DCompile(probe_source.data(),probe_source.size(),"actual-fused-resolve",nullptr,nullptr,"DualResolveProbe","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.p,&errors.p);
    if(FAILED(compiled)&&errors.p)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));Check(compiled,"actual resolve probe compile");
    Com<ID3D12PipelineState> resolve;D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=g.root.p;desc.CS={code->GetBufferPointer(),code->GetBufferSize()};
    Check(g.device->CreateComputePipelineState(&desc,IID_ID3D12PipelineState,resolve.address()),"actual resolve PSO");

    for(auto domain:{ModelDomain::DirectValue,ModelDomain::SrgbProxy})for(Scale scale:std::initializer_list<Scale>{{1,1},{3,4},{2,3},{1,2},{1,4}})for(bool changed:{false,true}) {
        std::vector<std::uint8_t> data(13*9*8);
        const std::uint16_t pixel[4]={0x3400,0x3800,0x3a00,0x3c00};
        for(unsigned i=0;i<13*9;++i)std::memcpy(data.data()+i*8,pixel,8);
        auto p1=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,13,9,data,8);
        auto plan=MakePlan({2,scale},{13,9},{});Require(bool(plan),"dual plan");std::string error;
        auto* pipelines=dx12::CreatePipelines(g.device.p,error);Require(pipelines!=nullptr,"dual pipeline");
        auto* slot=dx12::CreateSpatialSlot(pipelines,p1.resource.p,plan,domain,256ull<<20,error);Require(slot!=nullptr,"dual allocation");
        auto resources=dx12::GetSurfaces(slot);Require(resources.output!=resources.input,"dual surfaces independent");
        Require((plan.path==Path::DirectSecond)==(resources.input==p1.resource.p),"100 percent direct input");
        Admission a{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),plan.secondary,true,true,true,true};
        auto state=static_cast<std::uint32_t>(p1.state);DualEval eval{&g,resources,0,changed,false};
        auto invalid=a;invalid.creation_completion_proven=false;
        auto rejected=dx12::RecordSecond(pipelines,slot,g.list.p,state,plan,invalid,SyntheticSecond,&eval);
        Require(!rejected.gpu_work_recorded&&eval.calls==0,"pending creation does not call evaluate");
        Require(!dx12::DestroySpatialSlotAfterRetirement(slot,[](void*){return false;},nullptr)&&slot,"unproven retirement retained");
        auto result=dx12::RecordSecond(pipelines,slot,g.list.p,state,plan,a,SyntheticSecond,&eval);
        Require(result.HasSecond()&&eval.calls==1,"exactly one synthetic secondary evaluation");
        Texture resolved;
        if(resources.low_delta) {
            resolved=g.EmptyEditField(13,9,DXGI_FORMAT_R32G32B32A32_FLOAT);g.TextureState(resolved,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            g.Srv(2,p1.resource.p,DXGI_FORMAT_R16G16B16A16_FLOAT);g.Srv(12,resources.low_delta);
            g.UavTexture(kCodecResultUavIndex,resolved.resource.p,DXGI_FORMAT_R32G32B32A32_FLOAT);
            CodecConstants c{};c.model_width=13;c.model_height=9;c.dual_low_width=plan.secondary.width;c.dual_low_height=plan.secondary.height;c.dual_model_domain=unsigned(domain);
            g.list->SetDescriptorHeaps(1,&g.heap.p);g.list->SetComputeRootSignature(g.root.p);g.list->SetComputeRootDescriptorTable(0,g.heap->GetGPUDescriptorHandleForHeapStart());
            g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);g.list->SetPipelineState(resolve.p);g.list->Dispatch(2,2,1);g.ReadTexture(resolved);
        }
        Texture delta,input;if(resources.low_delta){delta=BorrowReadback(g,resources.low_delta);g.ReadTexture(delta);input=BorrowReadback(g,resources.input);g.ReadTexture(input);}
        g.Execute();
        if(resources.low_delta) {
            void* mapped=nullptr;Check(input.readback->Map(0,nullptr,&mapped),"map actual FP16 L1");
            const auto pixels=g.TexturePixels(delta);
            for(unsigned y=0;y<input.height;++y)for(unsigned x=0;x<input.width;++x) {
                const auto* half=reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(mapped)+input.footprint.Offset+y*input.footprint.Footprint.RowPitch+x*8);
                const auto decode=[domain](double v){return domain==ModelDomain::DirectValue?v:v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
                const auto pixel=pixels[y*input.width+x];const float actual[3]={pixel.x,pixel.y,pixel.z},output[3]={.5f,.25f,.75f};
                for(unsigned c=0;c<3;++c) {
                    const double expected=changed?decode(output[c])-decode(HalfToFloat(half[c])):0;
                    Require(std::abs(actual[c]-expected)<2e-6,"matched difference uses actual quantized L1");
                    if(!changed)Require(actual[c]==0,"zero edit exact");
                }
                Require(pixel.w==1,"valid residual alpha");if(changed)Require(pixel.x>0&&pixel.y<0,"signed channels retained");
            }
            input.readback->Unmap(0,nullptr);
            const auto final=g.TexturePixels(resolved);
            for(unsigned y=0;y<9;++y)for(unsigned x=0;x<13;++x) {
                const double px=(x+.5)*input.width/13.0-.5,py=(y+.5)*input.height/9.0-.5;
                const int ix=int(std::floor(px)),iy=int(std::floor(py));const double fx=px-ix,fy=py-iy;
                const float primary[3]={.25f,.5f,.75f};const auto got=final[y*13+x];const float values[3]={got.x,got.y,got.z};
                for(unsigned c=0;c<3;++c) {
                    double delta=0;
                    for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
                        auto v=pixels[std::clamp(iy+j,0,int(input.height)-1)*input.width+std::clamp(ix+i,0,int(input.width)-1)];
                        delta+=(i?fx:1-fx)*(j?fy:1-fy)*(c==0?v.x:c==1?v.y:v.z);
                    }
                    const auto decode=[domain](double v){return domain==ModelDomain::DirectValue?v:v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
                    const auto encode=[domain](double v){return domain==ModelDomain::DirectValue?v:v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;};
                    const double expected=delta==0?primary[c]:encode(decode(primary[c])+delta);
                    Require(std::abs(values[c]-expected)<3e-6,"actual fused restored output versus independent bilinear oracle");
                    if(!changed)Require(values[c]==primary[c],"fused zero edit returns P1 bit exact");
                }
                Require(got.w==1,"fused alpha preserved");
            }

        }
        Require(dx12::DestroySpatialSlotAfterRetirement(slot,CompletedDual,&g),"fence-proven spatial retirement");
        Require(dx12::DestroyPipelinesAfterRetirement(pipelines,CompletedDual,&g),"fence-proven pipeline retirement");++cases;
    }
    // Opaque failure records no follow-up timestamps, output barrier or residual.
    std::vector<std::uint8_t> data(8*8*8,0);auto p1=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,8,8,data,8);
    auto plan=MakePlan({2,{1,2}},{8,8},{});std::string error;
    auto* pipelines=dx12::CreatePipelines(g.device.p,error);Require(pipelines!=nullptr,"failure pipeline");
    auto* slot=dx12::CreateSpatialSlot(pipelines,p1.resource.p,plan,ModelDomain::DirectValue,256ull<<20,error);Require(slot!=nullptr,"failure slot");
    Admission a{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),plan.secondary,true,true,true,true};
    DualEval eval{&g,dx12::GetSurfaces(slot),0,false,true};auto state=static_cast<std::uint32_t>(p1.state);
    std::vector<unsigned> clocks;
    auto result=dx12::RecordSecond(pipelines,slot,g.list.p,state,plan,a,SyntheticSecond,&eval,
        [](void* u,ID3D12GraphicsCommandList*,unsigned q){static_cast<std::vector<unsigned>*>(u)->push_back(q);},&clocks);
    Require(result.selection==Selection::NoneAfterOpaqueFailure&&eval.calls==1,"opaque failure surfaced");
    Require(clocks==std::vector<unsigned>({2,3,4}),"no post-failure GPU timing or residual commands");
    auto retry=dx12::RecordSecond(pipelines,slot,g.list.p,state,plan,a,SyntheticSecond,&eval);
    Require(retry.selection==Selection::NoneAfterOpaqueFailure&&eval.calls==1,"opaque failure cannot retry");
    Require(!dx12::DestroySpatialSlotAfterRetirement(slot,CompletedDual,&g),"poisoned dependencies quarantined");
    // Never execute the failed recording. Retained objects die with this test process.
    Check(g.list->Close(),"discard failed recording");Check(g.allocator->Reset(),"discard allocator");
    Check(g.list->Reset(g.allocator.p,nullptr),"fresh test recording");
    std::printf("PASS dual spatial WARP: %u scale/domain/identity cases; real fences; opaque-failure quarantine; synthetic NR only\n",cases);
}
void ValidateGenericComposition(Gpu& gpu) {
    unsigned cases=0,changed=0;
    for(bool v2:{false,true})for(UINT flags:{0u,1u,32u,64u})for(int variant:{0,1,3,6,7}) {
        auto run=[&](RegionCase region){return Run(gpu,13,9,0,1,flags,variant,v2,7,5,{},0,{},1,{1,1,1,1},false,1,{}, {},region);};
        auto off=run({});RegionCase neutral;neutral.flags=32;auto on=run(neutral);
        Require(off.output.size()==on.output.size()&&!std::memcmp(off.output.data(),on.output.data(),off.output.size()*sizeof(Float4)),"generic neutral exact pixels");
        for(unsigned control=0;control<5;++control) {
            auto edited=neutral;edited.gains[control]=control?0.f:2.f;auto out=run(edited);
            for(std::size_t i=0;i<out.output.size();++i) {
                Require(std::isfinite(out.output[i].x)&&std::isfinite(out.output[i].y)&&std::isfinite(out.output[i].z),"generic finite output");
                Require(out.output[i].w==off.output[i].w,"generic preserves original alpha");
            }
            if(std::memcmp(off.output.data(),out.output.data(),off.output.size()*sizeof(Float4)))++changed;
            ++cases;
        }
    }
    Require(changed>0,"generic knobs affect real production composition");
    std::printf("PASS generic production WARP: %u control cases, %u changed outputs; neutral exact, finite and alpha preserved\n",cases,changed);
}
