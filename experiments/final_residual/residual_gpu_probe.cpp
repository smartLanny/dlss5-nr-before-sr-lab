// Reuse only the project-owned real hardware host/IO; never its old main.
#define main amd_probe_unused_main
#include "amd_of_gpu_probe.cpp"
#undef main
#include "residual_accumulator.hpp"

namespace {
std::uint16_t half(float value){_Float16 h=static_cast<_Float16>(value);std::uint16_t bits;std::memcpy(&bits,&h,2);return bits;}
float unhalf(const unsigned char* bytes){_Float16 h;std::memcpy(&h,bytes,2);return static_cast<float>(h);}
ComPtr<ID3D12Resource> upload_pixels(Host& host,unsigned w,unsigned h,DXGI_FORMAT format,unsigned bpp,const void* bytes){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> texture;
    hr(host.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"fixture texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;host.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);
    auto staging=host.buffer(size,D3D12_HEAP_TYPE_UPLOAD);unsigned char* mapped=nullptr;hr(staging->Map(0,nullptr,(void**)&mapped),"fixture map");
    for(unsigned y=0;y<h;++y)std::memcpy(mapped+y*fp.Footprint.RowPitch,static_cast<const unsigned char*>(bytes)+size_t(y)*w*bpp,size_t(w)*bpp);
    staging->Unmap(0,nullptr);host.begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};
    a.pResource=texture.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    b.pResource=staging.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
    host.list->CopyTextureRegion(&a,0,0,0,&b,nullptr);transition(host.list.Get(),texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);host.execute();return texture;
}
struct Picture {ComPtr<ID3D12Resource> original,enhanced;std::vector<unsigned char> base;std::vector<float> residual;};
Picture picture(Host& host,unsigned w,unsigned h,unsigned sequence,bool patch=false,int cut=0){
    Picture p;const int dx=int(sequence-1)*4,dy=int(sequence-1)*2;
    p.original=host.upload(w,h,dx,dy,cut);p.base=host.readback(p.original.Get(),4);
    if(patch){for(unsigned y=80;y<120;++y)for(unsigned x=100;x<150;++x)for(unsigned c=0;c<3;++c)p.base[(size_t(y)*w+x)*4+c]=240;
        p.original=upload_pixels(host,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,4,p.base.data());}
    std::vector<std::uint16_t> enhanced(size_t(w)*h*4);p.residual.resize(size_t(w)*h*3);
    const float gain=(sequence&1)?1.f:.25f;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const size_t pixel=size_t(y)*w+x;
        const float sign=((int(x)-dx+65536)/7)%2?1.f:-1.f;
        const float residual[]={.12f*gain,-.08f*gain,.06f*gain*sign};
        for(unsigned c=0;c<3;++c){const float base=p.base[pixel*4+c]/255.f;
            enhanced[pixel*4+c]=half(std::clamp(base+residual[c],0.f,1.f));
            p.residual[pixel*3+c]=unhalf(reinterpret_cast<unsigned char*>(&enhanced[pixel*4+c]))-base;}
        enhanced[pixel*4+3]=half(1.f);
    }
    p.enhanced=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,enhanced.data());return p;
}
Frame frame_for(unsigned sequence,unsigned stream=1){return {{stream,1,sequence},{1,1,kResidualSdrCodeValues,512,384,Backend::AmdOpticalFlow},sequence*1000ull,false};}
GpuFlow controlled(Host& host,const Frame& frame,const Plan& plan,ID3D12Resource* vectors,bool usable=true){
    GpuFlow flow;flow.token=plan.token;flow.vectors=vectors;flow.completion=host.done.Get();flow.value=host.value;
    flow.receipt={FlowStatus::Ready,Backend::AmdOpticalFlow,plan.history_generation,frame.source,plan.reference,plan.action==Action::ResetBoth,usable};return flow;
}
void verify_pixels(const Picture& picture,const std::vector<float>& previous,bool accumulate,
    const std::vector<unsigned char>& history,const std::vector<unsigned char>& output,unsigned sequence,
    bool patch,bool invalid_flow,const char* oracle,std::vector<float>& expected){
    constexpr unsigned w=512,h=384;expected.resize(size_t(w)*h*3);
    unsigned correct=0,total=0,positive=0,negative=0;double error=0;
    // Independent known-translation oracle; never read vectors to set expected
    // coordinates. This exposes a reversed flow even if both consumers agree.
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const size_t p=size_t(y)*w+x;
        const bool revealed=(patch&&x>=100&&x<150&&y>=80&&y<120)||
            (invalid_flow&&x>=104&&x<154&&y>=82&&y<122); // previous patch moves by (+4,+2), then disappears
        const bool invalid=invalid_flow&&x>=180&&x<240&&y>=80&&y<140;
        const bool use=accumulate&&x>=4&&y>=2&&!revealed&&!invalid;
        for(unsigned c=0;c<3;++c){const float desired=use?.5f*(picture.residual[p*3+c]+previous[((size_t(y)-2)*w+x-4)*3+c]):picture.residual[p*3+c];
            const auto quantized=half(desired);expected[p*3+c]=unhalf(reinterpret_cast<const unsigned char*>(&quantized));}
        if(x<64||x>=w-64||y<64||y>=h-64)continue;
        // Patch edges are photometric mixtures; measure its interior separately.
        if(patch&&x>=96&&x<154&&y>=76&&y<124&&!revealed)continue;
        bool match=true;
        for(unsigned c=0;c<3;++c){const float actual=unhalf(history.data()+p*8+c*2),wanted=expected[p*3+c];
            const float final=unhalf(output.data()+p*8+c*2),expected_final=std::clamp(picture.base[p*4+c]/255.f+wanted,0.f,1.f);
            const float e=std::abs(actual-wanted);error+=e;match=match&&std::isfinite(actual)&&e<.0012f&&std::abs(final-expected_final)<.0012f;
            if(actual>.01f)++positive;if(actual<-.01f)++negative;}
        ++total;if(match)++correct;
    }
    const double fraction=double(correct)/total;
    std::printf("residual source=%u accumulated=%d match=%.6f mean_error=%.8f positive=%u negative=%u\n",sequence,accumulate,fraction,error/(total*3),positive,negative);std::fflush(stdout);
    check(fraction>.97,oracle);check(positive&&negative,"actual FP16 history retains nonzero signed residuals");
    if(patch||invalid_flow){const unsigned x=patch?125:200,y=100;const auto p=size_t(y)*w+x;
        check(std::abs(unhalf(history.data()+p*8)-picture.residual[p*3])<.0012f,"REJECTION_RESEEDS_CURRENT_ORACLE");}
    if(invalid_flow){const auto p=size_t(100)*w+220;
        check(std::abs(unhalf(history.data()+p*8)-picture.residual[p*3])<.0012f,"out-of-range flow reseeds current residual");}
    if(accumulate&&std::strcmp(oracle,"RESIDUAL_TRANSLATION_ORACLE")==0){const auto p=size_t(100)*w;
        check(std::abs(unhalf(history.data()+p*8)-picture.residual[p*3])<.0012f,"newly exposed image border never clamps old history");}
}
void run_controlled(Host& host){
    constexpr unsigned w=512,h=384;HistoryContract history;ResidualAccumulator consumer(host.device.Get(),host.queue.Get(),w,h);
    check(consumer.initialized(),consumer.status());const Id bytes=consumer.allocated_texture_bytes();
    check(bytes>=Id(w)*h*32&&bytes<=Id(w)*h*32+5*65536,"five bounded textures: two FP16 histories, two originals, one output");
    std::vector<float> expected;unsigned submitted=0;ComPtr<ID3D12Resource> forbidden_alias;
    for(unsigned seq=1;seq<=6;++seq){Frame frame=frame_for(seq);if(seq==5)frame.scene_reset=true;
        auto plan=history.prepare(frame);check(plan.work(),"controlled source plan");
        auto picture_data=picture(host,w,h,seq,seq==3);
        std::vector<float> vectors(size_t(w)*h*2);
        for(size_t i=0;i<vectors.size();i+=2){vectors[i]=-4;vectors[i+1]=-2;}
        if(seq==4)for(unsigned y=80;y<140;++y)for(unsigned x=180;x<240;++x){vectors[(size_t(y)*w+x)*2]=x<210?std::nanf(""):10000.f;}
        auto texture=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,vectors.data());
        auto flow=controlled(host,frame,plan,texture.Get(),seq!=6);
        auto inputs=ResidualInputs{frame.source,frame.source,picture_data.original.Get(),picture_data.enhanced.Get()};
        if(seq==2){auto bad=inputs;bad.enhanced_source.sequence=1;
            check(consumer.submit(true,frame,plan,flow,bad)==ResidualSubmit::Rejected,"RESIDUAL_SOURCE_MISMATCH_ORACLE");
            auto wrong=flow;wrong.receipt.reference.sequence+=5;
            check(consumer.submit(true,frame,plan,wrong,inputs)==ResidualSubmit::Rejected,"wrong flow reference rejected before dispatch");
            wrong=flow;wrong.receipt.history_generation++;
            check(consumer.submit(true,frame,plan,wrong,inputs)==ResidualSubmit::Rejected,"wrong flow generation rejected");
            bad=inputs;bad.enhanced=forbidden_alias.Get();
            check(consumer.submit(true,frame,plan,flow,bad)==ResidualSubmit::Rejected,"rotating output cannot alias an immutable enhanced input");
            check(consumer.dispatch_count()==1,"mismatches never advance GPU history");}
        const auto decision=history.observe_flow(plan.token,flow.receipt);
        const bool accumulate=decision==FlowDecision::Accumulate;
        check(consumer.submit(true,frame,plan,flow,inputs)==ResidualSubmit::Submitted,consumer.status());++submitted;
        check(consumer.submit(true,frame,plan,flow,inputs)==ResidualSubmit::Duplicate,"repeated source does not submit twice");
        auto next_frame=frame;++next_frame.source.sequence;
        check(consumer.submit(true,next_frame,plan,flow,inputs)==ResidualSubmit::Busy,"unreleased output prevents resource and allocator reuse");
        check(consumer.dispatch_count()==submitted&&consumer.allocated_texture_bytes()==bytes,"bounded dispatch and allocation count");
        ResidualOutput out;check(consumer.poll(out)&&out.accumulated==accumulate,"residual availability decision");wait(out.completion,out.value);
        forbidden_alias=out.pixels;
        auto pixels=host.readback(out.pixels,8),signed_pixels=host.readback(out.signed_history,8);std::vector<float> next;
        verify_pixels(picture_data,expected,accumulate,signed_pixels,pixels,seq,seq==3,seq==4,"RESIDUAL_TRANSLATION_ORACLE",next);expected=std::move(next);
        if(seq==2||seq==3||seq==4){save("controlled-residual",100,seq,signed_pixels);save("controlled-final",100,seq,pixels);}
        check(!consumer.release_output(plan.token+1,host.done.Get(),host.value),"wrong consumer token cannot release");
        check(!consumer.release_output(plan.token,host.done.Get(),host.value+1),"future consumer completion cannot release");
        check(consumer.release_output(plan.token,host.done.Get(),host.value),"completed last reader releases output");
        check(history.finish(plan.token,plan.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"actual submitted residual adoption");
        check(history.prepare(frame).action==Action::Duplicate,"duplicate presentation does not rotate contract");
    }
    const Id before=consumer.dispatch_count();
    check(consumer.submit(false,{}, {}, {}, {})==ResidualSubmit::Disabled&&consumer.dispatch_count()==before,"disabled mode adds zero dispatch with no inputs");
    // Re-enable is a configuration generation change for BOTH histories.
    auto frame=frame_for(7);++frame.contract.config_generation;auto plan=history.prepare(frame);
    check(plan.action==Action::ResetBoth,"enable transition resets both histories through config generation");
    auto p=picture(host,w,h,7);std::vector<float> vectors(size_t(w)*h*2,0);
    auto texture=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,vectors.data());auto flow=controlled(host,frame,plan,texture.Get());
    check(history.observe_flow(plan.token,flow.receipt)==FlowDecision::Seed,"reenable flow contract seeds");
    check(consumer.submit(true,frame,plan,flow,{frame.source,frame.source,p.original.Get(),p.enhanced.Get()})==ResidualSubmit::Submitted,"reenable submit");
    ResidualOutput out;check(consumer.poll(out)&&!out.accumulated,"reenable never adopts stale residual");wait(out.completion,out.value);
    auto pixels=host.readback(out.pixels,8),signed_pixels=host.readback(out.signed_history,8);std::vector<float> next;
    verify_pixels(p,expected,false,signed_pixels,pixels,7,false,false,"REENABLE_SEED_ORACLE",next);
    check(consumer.release_output(plan.token,host.done.Get(),host.value),"reenable output retired");
    check(history.finish(plan.token,plan.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"reenable actual adoption");
    check(consumer.dispatch_count()==before+1&&consumer.allocated_texture_bytes()==bytes,"reenable one dispatch and no resource growth");
    std::printf("PASS controlled final residual dispatches=%llu texture_allocation_bytes=%llu\n",(unsigned long long)consumer.dispatch_count(),(unsigned long long)bytes);
}
void run_actual_amd(Host& host){
    constexpr unsigned w=512,h=384;HistoryContract history;ResidualAccumulator consumer(host.device.Get(),host.queue.Get(),w,h);
    AmdOfProducer producer(host.device.Get(),host.queue.Get(),w,h);
    check(consumer.initialized(),consumer.status());check(producer.initialized(),producer.status());
    std::vector<float> expected;unsigned usable=0,reseed=0;
    for(unsigned seq=1;seq<=12;++seq){const auto frame=frame_for(seq,2);const auto plan=history.prepare(frame);
        auto p=picture(host,w,h,seq,false,seq==11?1:seq==12?2:0);
        check(producer.dispatch(frame,plan,p.original.Get())==Submit::Submitted,"actual AMD producer dispatch");
        check(producer.dispatch(frame,plan,p.original.Get())==Submit::Duplicate,"actual AMD duplicate no dispatch");
        GpuFlow flow;check(producer.poll(flow),"actual AMD fence receipt");wait(flow.completion,flow.value);
        check(producer.poll(flow)&&flow.receipt.status==FlowStatus::Ready,"actual AMD completion");
        const auto decision=history.observe_flow(plan.token,flow.receipt);const bool accumulate=decision==FlowDecision::Accumulate;
        if(accumulate)++usable;else ++reseed;
        check(consumer.submit(true,frame,plan,flow,{frame.source,frame.source,p.original.Get(),p.enhanced.Get()})==ResidualSubmit::Submitted,consumer.status());
        ResidualOutput out;check(consumer.poll(out)&&out.accumulated==accumulate,"actual motion validity controls pixels");wait(out.completion,out.value);
        check(producer.release_output(plan.token,out.completion,out.value),"producer retained until real pixel consumer completed");
        auto pixels=host.readback(out.pixels,8),signed_pixels=host.readback(out.signed_history,8);std::vector<float> next;
        verify_pixels(p,expected,accumulate,signed_pixels,pixels,seq,false,false,"ACTUAL_AMD_RESIDUAL_ORACLE",next);expected=std::move(next);
        if(seq==7||seq==11){save("amd-residual",100,seq,signed_pixels);save("amd-final",100,seq,pixels);}
        check(consumer.release_output(plan.token,host.done.Get(),host.value),"actual AMD final output retired after readback");
        check(history.finish(plan.token,plan.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"actual residual history adopted");
        if(seq<=6||seq>=11)check(!accumulate,"AMD warmup and actual SCD reseed pixels");
    }
    check(usable==4&&reseed==8,"actual AMD warmup, motion and SCD exercised");
    check(producer.dispatch_count()==12&&consumer.dispatch_count()==12,"one OF and pixel dispatch per actual source");
    std::printf("PASS actual AMD plus final residual sources=12 accumulated=%u reseeded=%u\n",usable,reseed);
}
void residual_retirement(Host& host){
    constexpr unsigned w=512,h=384;auto p=picture(host,w,h,1);std::vector<float> v(size_t(w)*h*2,0);
    auto vectors=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,v.data());
    std::array<std::unique_ptr<ResidualAccumulator>,4> owners;
    for(auto& owner:owners){owner=std::make_unique<ResidualAccumulator>(host.device.Get(),host.queue.Get(),w,h);check(owner->initialized(),owner->status());}
    ComPtr<ID3D12Fence> gate;hr(host.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"residual pending gate");
    struct Unblock{ID3D12Fence* f;~Unblock(){f->Signal(1);}}unblock{gate.Get()};
    hr(host.queue->Wait(gate.Get(),1),"hold actual residual dispatches");
    for(unsigned i=0;i<4;++i){HistoryContract history;auto frame=frame_for(1,10+i);auto plan=history.prepare(frame);auto flow=controlled(host,frame,plan,vectors.Get());
        check(owners[i]->submit(true,frame,plan,flow,{frame.source,frame.source,p.original.Get(),p.enhanced.Get()})==ResidualSubmit::Submitted,"pending actual residual dispatch");
        ResidualOutput out;check(owners[i]->poll(out)&&out.completion->GetCompletedValue()<out.value,"controlled pending residual fence");
        check(!owners[i]->release_output(plan.token,out.completion,out.value),"pending output cannot reuse buffers");
        check(owners[i]->begin_retirement(plan.token,out.completion,out.value),"terminal owner records last use (no external reader)");owners[i].reset();}
    auto stats=ResidualAccumulator::collect_retired();check(stats.pending==4&&stats.active==0,"four submitted residual contexts retained");
    ResidualAccumulator refused(host.device.Get(),host.queue.Get(),w,h);
    check(!refused.initialized()&&refused.allocated_texture_bytes()==0,"fifth context refused before resource allocation");
    hr(gate->Signal(1),"unblock residual queue");hr(host.queue->Signal(host.done.Get(),++host.value),"residual completion barrier");wait(host.done.Get(),host.value);
    stats=ResidualAccumulator::collect_retired();check(stats.active+stats.pending+stats.quarantined==0,"exact completion retires bounded resources");
    {
        auto owner=std::make_unique<ResidualAccumulator>(host.device.Get(),host.queue.Get(),w,h);
        HistoryContract history;auto frame=frame_for(1,30);auto plan=history.prepare(frame);auto flow=controlled(host,frame,plan,vectors.Get());
        check(owner->submit(true,frame,plan,flow,{frame.source,frame.source,p.original.Get(),p.enhanced.Get()})==ResidualSubmit::Submitted,"independent consumer test dispatch");
        ResidualOutput out;check(owner->poll(out),"independent consumer output");wait(out.completion,out.value);
        ComPtr<ID3D12Fence> reader,hold;
        hr(host.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&reader)),"independent consumer fence");
        hr(host.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold)),"independent consumer gate");
        struct ReaderUnblock{ID3D12Fence* f;~ReaderUnblock(){f->Signal(1);}}release{hold.Get()};
        hr(host.queue->Wait(hold.Get(),1),"hold external reader completion");
        // Actual output read is submitted behind the gate, with resources held
        // until its own fence. Producer completion is already signaled.
        auto desc=out.pixels->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
        host.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
        auto readback=host.buffer(size,D3D12_HEAP_TYPE_READBACK);host.begin();
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
        src.pResource=out.pixels;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;host.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        hr(host.list->Close(),"external reader close");ID3D12CommandList* lists[]={host.list.Get()};host.queue->ExecuteCommandLists(1,lists);
        hr(host.queue->Signal(reader.Get(),1),"actual last reader pending signal");
        check(!owner->release_output(plan.token,reader.Get(),1),"completed producer cannot replace pending last reader");
        check(owner->begin_retirement(plan.token,reader.Get(),1),"attach separate last-reader fence");owner.reset();
        check(ResidualAccumulator::collect_retired().pending==1,"private histories retained until independent reader completes");
        hr(hold->Signal(1),"release actual last reader");wait(reader.Get(),1);
        stats=ResidualAccumulator::collect_retired();check(stats.active+stats.pending+stats.quarantined==0,"independent reader completion reclaims resources");
    }
    std::puts("PASS bounded residual retirement: four pending, fifth refused, completion reclaims all");
}
}
int main(){try{Host host;run_controlled(host);run_actual_amd(host);residual_retirement(host);host.validate_debug();
    std::printf("PASS final signed residual checks=%u actual_AMD_sources=12 residual_dispatches=24\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL %s checks=%u\n",e.what(),checks);std::fflush(stdout);return 1;}}
