// Real GPU experiment, deterministic synthetic original images, no Core/NGX.
#include "amd_of_producer.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace nr::final_residual;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
void check(bool x,const char* m){++checks;if(!x)throw std::runtime_error(m);}
void hr(HRESULT r,const char* m){if(FAILED(r)){std::printf("HRESULT %08lx\n",(unsigned long)r);throw std::runtime_error(m);}}
void wait(ID3D12Fence* f,UINT64 value){
    if(f->GetCompletedValue()==UINT64_MAX)throw std::runtime_error("device removed");
    if(f->GetCompletedValue()>=value)return;
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(e!=nullptr,"event");
    hr(f->SetEventOnCompletion(value,e),"completion event");
    // On timeout, do not close an event still registered with the GPU.
    check(WaitForSingleObject(e,10000)==WAIT_OBJECT_0,"GPU completion timeout: no retirement");CloseHandle(e);
    check(f->GetCompletedValue()!=UINT64_MAX&&f->GetCompletedValue()>=value,"exact completion");
}
void transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);
}
float texture_noise(int x,int y,int scale){
    const unsigned px=unsigned(x+65536),py=unsigned(y+65536),ix=px/scale,iy=py/scale;
    auto hash=[](unsigned a,unsigned b){unsigned z=a*0x9e3779b9u^b*0x85ebca6bu;z^=z>>16;z*=0x7feb352du;z^=z>>15;z*=0x846ca68bu;z^=z>>16;return float(z&255u);};
    const float fx=float(px%scale)/scale,fy=float(py%scale)/scale;
    return (hash(ix,iy)*(1-fx)+hash(ix+1,iy)*fx)*(1-fy)+(hash(ix,iy+1)*(1-fx)+hash(ix+1,iy+1)*fx)*fy;
}
void save(const char* prefix,unsigned scale,unsigned frame,const std::vector<unsigned char>& bytes){
    char path[128];std::snprintf(path,sizeof(path),"%s-%u-%u.bin",prefix,scale,frame);FILE* f=std::fopen(path,"wb");
    check(f!=nullptr,"open GPU evidence output");check(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size(),"save actual GPU readback");std::fclose(f);
}
struct Host {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> done;ComPtr<ID3D12InfoQueue> debug;
    UINT64 value=0;
    Host(){
        ComPtr<ID3D12Debug> dbg;bool enabled=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)));
        if(enabled)dbg->EnableDebugLayer();std::printf("debug_layer=%d\n",enabled);
        ComPtr<IDXGIFactory6> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);
            if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&std::wcsstr(d.Description,L"5090")){
                hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"hardware device");
                char name[256]{};WideCharToMultiByte(CP_UTF8,0,d.Description,-1,name,sizeof(name),nullptr,nullptr);
                std::printf("adapter=%s vendor=%04x device=%04x luid=%08lx:%08lx\n",name,d.VendorId,d.DeviceId,(unsigned long)d.AdapterLuid.HighPart,(unsigned long)d.AdapterLuid.LowPart);break;
            }
        }
        check(device!=nullptr,"required 5090D hardware unavailable; never substitute WARP");
        if(enabled)device.As(&debug);
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        hr(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&alloc)),"allocator");hr(device->CreateCommandList(0,q.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        hr(list->Close(),"initial close");hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)),"fence");
    }
    void begin(){wait(done.Get(),value);hr(alloc->Reset(),"reset alloc");hr(list->Reset(alloc.Get(),nullptr),"reset list");}
    void execute(){hr(list->Close(),"close list");ID3D12CommandList* ls[]={list.Get()};queue->ExecuteCommandLists(1,ls);hr(queue->Signal(done.Get(),++value),"host signal");wait(done.Get(),value);}
    ComPtr<ID3D12Resource> buffer(UINT64 size,D3D12_HEAP_TYPE heap){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES p{};p.Type=heap;ComPtr<ID3D12Resource> r;
        hr(device->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&d,heap==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)),"staging buffer");return r;
    }
    ComPtr<ID3D12Resource> upload(unsigned w,unsigned h,int dx,int dy,int cut){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_HEAP_PROPERTIES p{};p.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
        hr(device->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)),"source texture");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&f,nullptr,nullptr,&bytes);
        auto u=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);unsigned char* data=nullptr;hr(u->Map(0,nullptr,(void**)&data),"upload map");
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const int xx=int(x)-dx,yy=int(y)-dy;
            // Broad deterministic texture, with enough low-frequency structure
            // for both 100% and true area-filtered 50% SDK input.
            unsigned char v=(unsigned char)(.60f*texture_noise(xx,yy,8)+.25f*texture_noise(xx,yy,32)+.15f*texture_noise(xx,yy,2));
            if(cut==1)v=250;else if(cut==2)v=4;
            auto* pixel=data+y*f.Footprint.RowPitch+x*4;pixel[0]=pixel[1]=pixel[2]=v;pixel[3]=255;
        }
        u->Unmap(0,nullptr);begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=r.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource=u.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=f;list->CopyTextureRegion(&a,0,0,0,&b,nullptr);
        transition(list.Get(),r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);execute();return r;
    }
    std::vector<unsigned char> readback(ID3D12Resource* src,unsigned bpp){
        auto d=src->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&f,nullptr,nullptr,&bytes);
        auto rb=buffer(bytes,D3D12_HEAP_TYPE_READBACK);begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=rb.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=f;
        b.pResource=src;b.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&a,0,0,0,&b,nullptr);execute();
        unsigned char* data=nullptr;hr(rb->Map(0,nullptr,(void**)&data),"readback map");std::vector<unsigned char> out(size_t(d.Width)*d.Height*bpp);
        for(unsigned y=0;y<d.Height;++y)std::memcpy(out.data()+y*d.Width*bpp,data+y*f.Footprint.RowPitch,size_t(d.Width)*bpp);rb->Unmap(0,nullptr);return out;
    }
    void validate_debug(){
        if(!debug){std::puts("debug_messages=unavailable");return;}
        unsigned errors=0;for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<char> b(n);auto* m=(D3D12_MESSAGE*)b.data();
            if(SUCCEEDED(debug->GetMessage(i,m,&n))&&m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::printf("D3D12_ERROR %s\n",m->pDescription);}}
        std::printf("debug_errors=%u\n",errors);check(errors==0,"D3D12 validation errors");
    }
};
void scenario(Host& host,unsigned percent){
    constexpr unsigned w=512,h=384;AmdOfProducer producer(host.device.Get(),host.queue.Get(),w,h,percent);
    std::printf("scale=%u status=%s\n",percent,producer.status());check(producer.initialized(),"actual FFX init");HistoryContract history;
    for(unsigned seq=1;seq<=24;++seq){
        Frame frame{{1,1,seq},{1,1,1,w,h,Backend::AmdOpticalFlow},seq*1000ull,false};
        if(seq==14)frame.scene_reset=true;
        auto plan=history.prepare(frame);check(plan.work(),"source plan");
        const bool reset=plan.action==Action::ResetBoth;
        const int dx=seq<=20?int(seq-1)*4:0,dy=seq<=20?int(seq-1)*2:0;
        const int cut=seq==21?1:seq>=22?2:0;
        auto src=host.upload(w,h,dx,dy,cut);
        if(seq==2){auto bad=plan;bad.reference.sequence+=10;check(producer.dispatch(frame,bad,src.Get())==Submit::Rejected,"wrong reference pre-dispatch rejection");check(producer.dispatch_count()==1,"wrong reference never advances vendor");}
        check(producer.dispatch(frame,plan,src.Get())==Submit::Submitted,"real OF dispatch");
        check(producer.dispatch(frame,plan,src.Get())==Submit::Duplicate,"duplicate FG display never dispatches");
        check(producer.dispatch_count()==seq,"one actual dispatch per source");
        GpuFlow flow{};check(producer.poll(flow),"pending receipt");wait(flow.completion,flow.value);check(producer.poll(flow)&&flow.receipt.status==FlowStatus::Ready,"exact ready receipt");
        check(history.observe_flow(plan.token,flow.receipt)==(reset?FlowDecision::Seed:flow.receipt.motion_usable?FlowDecision::Accumulate:FlowDecision::ReseedResidual),"real flow history receipt");
        auto data=host.readback(flow.vectors,8);auto scd=host.readback(flow.scene_data,4);std::array<unsigned,3> cuts{};std::memcpy(cuts.data(),scd.data(),12);
        save("flow-rg32",percent,seq,data);save("scene-r32",percent,seq,scd);
        std::vector<float> xs,ys;double error=0;unsigned correct=0,n=0;float maximum=0;bool finite=true;
        for(unsigned y=64;y<h-64;++y)for(unsigned x=64;x<w-64;++x){float v[2];std::memcpy(v,data.data()+(size_t(y)*w+x)*8,8);finite=finite&&std::isfinite(v[0])&&std::isfinite(v[1]);
            xs.push_back(v[0]);ys.push_back(v[1]);maximum=std::max(maximum,std::max(std::abs(v[0]),std::abs(v[1])));
            if((seq>=7&&seq<=12)||seq==20){const double e=std::hypot(v[0]+4,v[1]+2);error+=e;if(e<=2.5)++correct;++n;}}
        std::sort(xs.begin(),xs.end());std::sort(ys.begin(),ys.end());
        check(finite,"finite real flow");
        std::printf("scale=%u source=%u reference=%llu generation=%llu reset=%d age=%u usable=%d median=(%.3f,%.3f) mean_epe=%.4f within2.5=%.4f max=%.3f scd=[%u,%u,%u]\n",
            percent,seq,(unsigned long long)flow.receipt.reference.sequence,(unsigned long long)plan.history_generation,reset,flow.frames_since_reset,flow.receipt.motion_usable,xs[xs.size()/2],ys[ys.size()/2],n?error/n:0,n?double(correct)/n:0,maximum,cuts[0],cuts[1],cuts[2]);std::fflush(stdout);
        if(reset||flow.frames_since_reset<=5)check(maximum==0,"reset exposes zero flow");
        if((seq>=7&&seq<=12)||seq==20){check(std::abs(xs[xs.size()/2]+4)<=1&&std::abs(ys[ys.size()/2]+2)<=1,"current-to-reference source-pixel direction and scale");check(double(correct)/n>.80,"translation flow spatial agreement");}
        if(seq==21||seq==22)check(!flow.receipt.motion_usable,"SDK scene-cut output forbids accumulation");
        check(!producer.release_output(plan.token+1,host.done.Get(),host.value),"wrong token cannot release live output");
        check(!producer.release_output(plan.token,host.done.Get(),host.value+1),"future consumer fence cannot release");
        check(producer.release_output(plan.token,host.done.Get(),host.value),"exact consumer retires output");
        // Actual OF advanced at frame 12, but residual was deliberately not adopted.
        const auto outcome=seq==12?ResidualStatus::SubmittedNotAdopted:ResidualStatus::SubmittedAdopted;
        check(history.finish(plan.token,plan.history_generation,outcome)==(seq==12?Completion::ResetRequired:Completion::Adopted),"adoption contract");
        check(history.prepare(frame).action==Action::Duplicate,"duplicate display does not rotate residual");
    }
}
}
int main(){
    try{Host host;scenario(host,100);scenario(host,50);host.validate_debug();std::printf("PASS checks=%u actual_amd_dispatches=48\n",checks);return 0;}
    catch(const std::exception& e){std::printf("FAIL %s checks=%u\n",e.what(),checks);std::fflush(stdout);return 1;}
}
