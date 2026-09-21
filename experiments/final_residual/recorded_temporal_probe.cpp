#define FINAL_RESIDUAL_NO_MAIN
#include "residual_gpu_probe.cpp"
#include "recorded_temporal.hpp"

namespace {
float stored(float v){const auto h=half(v);return unhalf(reinterpret_cast<const unsigned char*>(&h));}
void policy_checks(){
    Settings s;check(!s.enabled&&s.scope==Scope::WholeChain,"new feature defaults OFF/whole chain");
    s.enabled=true;check(std::abs(HistoryWeight(s,39.)-.5f)<1e-6,"65 percent reliability gives 39 ms effective half life");
    check(std::abs(HistoryWeight(s,13.)*HistoryWeight(s,26.)-.5f)<1e-6,"elapsed source time is frame-rate independent");
    for(double dt:{0.,-1.,251.,std::numeric_limits<double>::infinity()})check(HistoryWeight(s,dt)==0,"invalid/gap does not accumulate");
    s.validation=Validation::Legacy;check(HistoryWeight(s,33)==.5f,"legacy independent fixed weight preserved");
    HistoryContract h;auto f=frame_for(1);f.contract.backend=Backend::NativeMotion;auto p=h.prepare(f);
    OrderedFlow o{Backend::NativeMotion,p.history_generation,f.source,p.reference,true};
    auto wrong=o;wrong.current.epoch++;check(!h.observe_ordered_recording(p.token,wrong),"recorded wrong epoch rejected");
    check(h.observe_ordered_recording(p.token,o)&&!h.has_history(),"recording is not adoption");
    check(h.prepare(frame_for(2)).action==Action::Busy,"recorded transaction remains pending");
    check(!h.cancel_before_flow(p.token),"recorded work cannot masquerade as unrecorded");
    check(h.finish(p.token,p.history_generation,ResidualStatus::NotSubmitted)==Completion::ResetRequired,"cancelled recording resets history");
}
void native_pixels(Host& host,Scope scope){
    constexpr unsigned w=129,h=97,tw=w+8,th=h+6;
    RecordedTemporal owner(host.device.Get(),host.queue.Get(),w,h,Backend::NativeMotion,scope,128ull<<20);
    check(owner.initialized(),owner.status());
    std::vector<float> previous(w*h*3);Id timestamp=1000;
    for(unsigned seq=1;seq<=7;++seq){
        const unsigned dt=seq==5?300000:seq==3?33000:16000;timestamp+=dt;
        std::vector<std::uint16_t> base(w*h*4),anchor(w*h*4),target(tw*th*4,half(.875f));
        std::vector<float> current(w*h*3),anchor_values(w*h*3);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto p=y*w+x;
            for(unsigned c=0;c<3;++c){const float b=.4f+.03f*c;const float a=scope==Scope::LaterLayers?(c==0?(seq%2?.12f:.04f):-.02f):0;
                // Later layers partly cancel the first layer, with moving signed detail.
                const float edit=(c==0?-.08f:c==1?.06f:((int(x)-int(seq)*4+1024)/8)%2?.02f:-.02f)*(seq%2?1:.3f);
                base[p*4+c]=half(b);anchor[p*4+c]=half(stored(b)+a);
                const float stable_base=scope==Scope::LaterLayers?stored(stored(b)+a):stored(b);
                target[((y+2)*tw+x+3)*4+c]=half(stable_base+edit);
                current[p*3+c]=stored(stable_base+edit)-stable_base;anchor_values[p*3+c]=stable_base;
            }base[p*4+3]=anchor[p*4+3]=half(1);target[((y+2)*tw+x+3)*4+3]=half(1);
        }
        auto b=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,base.data());
        auto z=upload_pixels(host,tw,th,DXGI_FORMAT_R16G16B16A16_FLOAT,8,target.data(),true);
        const unsigned mw=69,mh=53;std::vector<float> mv(mw*mh*2,99.f);
        for(unsigned y=1;y<50;++y)for(unsigned x=2;x<67;++x){mv[(y*mw+x)*2]=-2;mv[(y*mw+x)*2+1]=-1;}
        auto motion=upload_pixels(host,mw,mh,DXGI_FORMAT_R32G32_FLOAT,8,mv.data());
        RecordedInputs in;in.settings.enabled=true;in.settings.scope=scope;in.layers=scope==Scope::LaterLayers?5:1;
        in.frame={{9+unsigned(scope),1,seq},{1,1,1,w,h,Backend::NativeMotion,scope,{0,0,w,h,1,1}},timestamp,seq==6};
        in.original=b.Get();in.original_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        in.candidate=z.Get();in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;in.candidate_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        in.candidate_x=3;in.candidate_y=2;
        in.native={in.frame.source,motion.Get(),DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_SOURCE,2,1,65,49,2,2,true};
        if(scope==Scope::LaterLayers){
            auto a=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,anchor.data());host.begin();
            transition(host.list.Get(),owner.anchor(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            host.list->CopyResource(owner.anchor(),a.Get());transition(host.list.Get(),owner.anchor(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);host.execute();
            in.anchor_source=in.frame.source;
        }
        host.begin();if(seq==1){auto bad=in;bad.settings.enabled=false;check(owner.record(host.list.Get(),bad)==RecordResult::Disabled,"OFF is before recording");
            bad=in;bad.native.source.sequence++;check(owner.record(host.list.Get(),bad)==RecordResult::Unavailable,"wrong native source rejected");
            if(scope==Scope::LaterLayers){bad=in;bad.layers=1;check(owner.record(host.list.Get(),bad)==RecordResult::Unavailable,"later scope unavailable single layer");
                bad=in;bad.anchor_source={};check(owner.record(host.list.Get(),bad)==RecordResult::Unavailable,"missing anchor never changes scope");}
            check(owner.stats().recorded==0,"failed admission leaves workload unchanged");
        }
        check(owner.record(host.list.Get(),in)==RecordResult::Recorded,owner.status());
        check(owner.stats().submitted==seq-1,"recorded not yet submitted");
        check(owner.record(host.list.Get(),in)==RecordResult::Busy,"pending source never records twice");
        check(!owner.submitted(host.done.Get(),0),"missing actual completion receipt refused");
        host.execute();check(owner.submitted(host.done.Get(),host.value),"actual host Execute+Signal adoption");
        check(owner.record(host.list.Get(),in)==RecordResult::Duplicate,"FG duplicate does not advance history");
        const auto pixels=host.readback(z.Get(),8);const bool use=seq>1&&seq!=5&&seq!=6;
        const float weight=std::exp2(-float(dt)/39000.f);
        for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){const bool inside=x>=3&&x<w+3&&y>=2&&y<h+2;
            for(unsigned c=0;c<3;++c){float expected=.875f;
                if(inside){const auto p=(y-2)*w+x-3;float r=current[p*3+c];
                    if(use&&x>=7&&y>=4)r=r*(1-weight)+previous[((y-4)*w+x-7)*3+c]*weight;
                    current[p*3+c]=stored(r);expected=stored(std::clamp(anchor_values[p*3+c]+r,0.f,1.f));}
                check(std::abs(unhalf(pixels.data()+(y*tw+x)*8+c*2)-expected)<.0012f,
                    scope==Scope::LaterLayers?"LATER_COMPOSED_ANCHOR_ORACLE":"RECORDED_NATIVE_PIXEL_ORACLE");
            }
        }
        previous=std::move(current);
    }
    check(owner.stats().submitted==7&&owner.stats().seeded==3,"real frame counts and gap/reset seeds");
    std::printf("PASS recorded native scope=%u frames=7 odd_extent=129x97 subrect=3,2 cancellation_counterexample=%u\n",unsigned(scope),unsigned(scope==Scope::LaterLayers));
}
void amd_pixels(Host& host){
    constexpr unsigned w=512,h=384;RecordedTemporal owner(host.device.Get(),host.queue.Get(),w,h,Backend::AmdOpticalFlow,Scope::WholeChain,256ull<<20);
    check(owner.initialized(),owner.status());std::vector<float> previous(w*h*3);unsigned accumulate=0;
    for(unsigned seq=1;seq<=12;++seq){auto picture_data=picture(host,w,h,seq,false,seq==12?1:0);
        std::vector<std::uint16_t> base(w*h*4),target(w*h*4);
        for(unsigned i=0;i<w*h;++i){for(unsigned c=0;c<3;++c){base[i*4+c]=half(picture_data.base[i*4+c]/255.f);
            target[i*4+c]=half(stored(picture_data.base[i*4+c]/255.f)+picture_data.residual[i*3+c]);}base[i*4+3]=target[i*4+3]=half(1);}
        auto b=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,base.data());
        auto z=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,target.data(),true);
        RecordedInputs in;in.settings.enabled=true;in.frame=frame_for(seq,33);in.frame.source_time=seq*16000;
        in.original=b.Get();in.original_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        in.candidate=z.Get();in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;in.candidate_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        host.begin();check(owner.record(host.list.Get(),in)==RecordResult::Recorded,owner.status());
        host.execute();check(owner.submitted(host.done.Get(),host.value),"AMD recorded last-consumer submission");
        auto pixels=host.readback(z.Get(),8);const bool use=seq>=7&&seq<=11;const float weight=std::exp2(-16.f/39.f);
        unsigned matched=0,total=0;std::vector<float> next(w*h*3);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto p=y*w+x;bool ok=true;
            for(unsigned c=0;c<3;++c){const float bvalue=unhalf(reinterpret_cast<unsigned char*>(&base[p*4+c]));
                const float cur=unhalf(reinterpret_cast<unsigned char*>(&target[p*4+c]))-bvalue;
                float r=cur;if(use&&x>=4&&y>=2)r=cur*(1-weight)+previous[((y-2)*w+x-4)*3+c]*weight;
                next[p*3+c]=stored(r);ok&=std::abs(unhalf(pixels.data()+p*8+c*2)-stored(std::clamp(bvalue+r,0.f,1.f)))<.0015f;
            }if(x>=64&&x<w-64&&y>=64&&y<h-64){++total;if(ok)++matched;}
        }
        const float fraction=float(matched)/total;std::printf("AMD recorded frame=%u expected_accumulate=%u pixel_match=%.6f\n",seq,unsigned(use),fraction);
        check(fraction>.97f,use?"RECORDED_AMD_DIRECTION_AND_WEIGHT_ORACLE":"GPU_WARMUP_SCD_SEED_ORACLE");
        if(use)++accumulate;previous=std::move(next);
    }check(accumulate==5,"real AMD GPU warmup/accumulation/cut sequence");
}
void cancel_and_budget(Host& host){
    for(Backend backend:{Backend::NativeMotion,Backend::AmdOpticalFlow}){
        constexpr unsigned w=128,h=128;auto owner=std::make_unique<RecordedTemporal>(host.device.Get(),host.queue.Get(),w,h,backend,Scope::WholeChain,128ull<<20);
        check(owner->initialized(),owner->status());std::vector<std::uint16_t> data(w*h*4,half(.5f));std::vector<float> flow(w*h*2,0);
        auto b=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,data.data());auto z=upload_pixels(host,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8,data.data(),true);
        auto mv=upload_pixels(host,w,h,DXGI_FORMAT_R32G32_FLOAT,8,flow.data());RecordedInputs in;in.settings.enabled=true;
        in.frame={{41,1,1},{1,1,1,w,h,backend},16000,false};in.original=b.Get();in.original_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        in.candidate=z.Get();in.candidate_format=DXGI_FORMAT_R16G16B16A16_FLOAT;in.candidate_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
        in.native={in.frame.source,mv.Get(),DXGI_FORMAT_R32G32_FLOAT,D3D12_RESOURCE_STATE_COPY_SOURCE,0,0,w,h,1,1,true};
        host.begin();check(owner->record(host.list.Get(),in)==RecordResult::Recorded,owner->status());
        hr(host.list->Close(),"cancel close");hr(host.list->Reset(host.alloc.Get(),nullptr),"actual unsubmitted Reset");hr(host.list->Close(),"cancel reset close");
        check(owner->cancelled_unsubmitted(),"proven cancellation accepted");check(!owner->can_record(),"cancelled FFX/temporal state cannot be reused");owner.reset();
        auto s=RecordedTemporal::collect_retired();check(s.active+s.pending+s.quarantined==0,"unsubmitted cancellation reclaims safely");
    }
    RecordedTemporal small(host.device.Get(),host.queue.Get(),1920,1080,Backend::NativeMotion,Scope::WholeChain,1024);
    check(!small.initialized()&&small.stats().bytes==0,"insufficient budget refused before texture allocation");
}
}
int main(){try{std::setvbuf(stdout,nullptr,_IONBF,0);policy_checks();Host host;native_pixels(host,Scope::WholeChain);native_pixels(host,Scope::LaterLayers);
    amd_pixels(host);cancel_and_budget(host);host.validate_debug();
    std::printf("PASS recorded Core seam checks=%u; GPU tests, no game claim\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL %s checks=%u\n",e.what(),checks);return 1;}}
