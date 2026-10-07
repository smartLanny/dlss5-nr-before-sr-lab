// Real D3D12 copy/bytes and exact fence. Native callback is an explicitly
// controlled CopyResource consumer, NOT NVIDIA Feature13 inference.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "nr_rr_identity_dx12.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace nr_rr_identity;
namespace {
unsigned checks=0;
void Need(bool p,const char* message){++checks;if(!p)throw std::runtime_error(message);}
void Hr(HRESULT h){Need(SUCCEEDED(h),"D3D12 operation failed");}
template<class T>struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T** out(){return &p;}
    T* operator->()const{return p;}operator T*()const{return p;}
};
void Barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,unsigned a,unsigned b){
    D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        static_cast<D3D12_RESOURCE_STATES>(a),static_cast<D3D12_RESOURCE_STATES>(b)};
    l->ResourceBarrier(1,&v);
}
constexpr unsigned read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
struct Rig {
    Com<IDXGIFactory4> factory;Com<IDXGIAdapter> adapter;Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;Com<ID3D12Fence> fence;
    HANDLE event=nullptr;Id serial=1;
    Rig(){
        Hr(CreateDXGIFactory1(IID_PPV_ARGS(factory.out())));
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.out())));
        Hr(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(device.out())));
        D3D12_COMMAND_QUEUE_DESC q{};Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(queue.out())));
        Hr(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(allocator.out())));
        Hr(device->CreateCommandList(0,q.Type,allocator,nullptr,IID_PPV_ARGS(list.out())));
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(fence.out())));
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Need(event!=nullptr,"event");
    }
    ~Rig(){if(event)CloseHandle(event);}
    void Submit(){
        Hr(list->Close());ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);
        Hr(queue->Signal(fence,serial));Hr(fence->SetEventOnCompletion(serial,event));
        Need(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"host completion deadline");
    }
    void Reset(){Hr(allocator->Reset());Hr(list->Reset(allocator,nullptr));++serial;}
    ID3D12Resource* Texture(unsigned format){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=17;d.Height=11;d.MipLevels=d.DepthOrArraySize=d.SampleDesc.Count=1;
        d.Format=static_cast<DXGI_FORMAT>(format);ID3D12Resource* p=nullptr;
        Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,
            static_cast<D3D12_RESOURCE_STATES>(read),nullptr,IID_PPV_ARGS(&p)));return p;
    }
    ID3D12Resource* Buffer(UINT64 bytes,bool upload){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* p=nullptr;Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,
            upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p)));return p;
    }
};
struct Params {
    // Two overload views. Unified storage models the documented NGX key store;
    // split storage deliberately creates the alias rollback negative control.
    std::map<std::string,void*> typed,untyped;
    bool unified=true,drop_write=false,break_restore=false;void* original=nullptr;
    static Result Read(void* p,const char* k,bool typed,void** v){
        auto& x=*static_cast<Params*>(p);const auto& m=typed?x.typed:x.untyped;
        const auto i=m.find(k);*v=i==m.end()?nullptr:i->second;return i==m.end()?0xBAD00004u:1u;
    }
    static void Write(void* p,const char* k,bool typed,void* v){
        auto& x=*static_cast<Params*>(p);
        if(x.drop_write||(x.break_restore&&v==x.original))return;
        (typed?x.typed:x.untyped)[k]=v;
        if(x.unified){auto& other=typed?x.untyped:x.typed;
            if(other.contains(k))other[k]=v;}
    }
    Parameters Api(){return {this,Read,Write};}
};
struct Lease {
    Rig* rig=nullptr;Frame current{};Recording recorded{};
    ID3D12Resource *a=nullptr,*b=nullptr;
    bool active=false,submitted=false,released=false,poisoned=false;
    bool current_ok=true,record_ok=true,completed_visible=true,removed=false,native_started=false,mark_ok=true;
    unsigned records=0,retirements=0,queries=0,captures=0,restores=0;
    bool envelope_ok=true,restore_ok=true;
    // Explicitly a controlled host adapter, not the Core's NrSlot ledger.
    LeaseAdapter Api(){return {this,
        [](void* p,const Frame& f){auto& x=*static_cast<Lease*>(p);++x.queries;
            return x.current_ok&&f.recording==x.current.recording&&f.source==x.current.source&&
                f.view==x.current.view&&f.module_generation==x.current.module_generation&&
                f.feature_generation==x.current.feature_generation&&f.color.resource==x.current.color.resource;},
        [](void* p,const Frame& f,ID3D12Resource* a,ID3D12Resource* b)->void*{
            auto& x=*static_cast<Lease*>(p);if(!x.record_ok||x.active||x.poisoned||!(f.recording==x.current.recording))return nullptr;
            x.active=true;x.submitted=x.released=false;x.native_started=false;
            x.recorded=f.recording;x.a=a;x.b=b;a->AddRef();b->AddRef();++x.records;return p;},
        [](void* p,void*){auto& x=*static_cast<Lease*>(p);x.native_started=x.mark_ok;return x.mark_ok;},
        [](void*,void*,unsigned* state){*state=read;return true;},
        [](void* p,void*){const auto& x=*static_cast<Lease*>(p);return !x.poisoned&&!x.removed;},
        [](void* p,void*){auto& x=*static_cast<Lease*>(p);
            if(x.removed){x.poisoned=true;return false;}
            return !x.poisoned&&x.active&&x.submitted&&x.released&&x.completed_visible&&
                nr_private_sr::Complete({fqueue(x),reinterpret_cast<Id>(x.rig->fence.p),x.recorded.reset},
                    {fqueue(x),reinterpret_cast<Id>(x.rig->fence.p),x.rig->fence->GetCompletedValue()});},
        [](void* p,void*){auto& x=*static_cast<Lease*>(p);Need(x.active,"lease release pairs acquire");
            x.a->Release();x.b->Release();x.a=x.b=nullptr;x.active=false;++x.retirements;},
        [](void* p,void*){static_cast<Lease*>(p)->poisoned=true;}};}
    static Id fqueue(const Lease& x){return x.recorded.queue;}
    Envelope State(){return {this,
        [](void* p,ID3D12GraphicsCommandList*){auto& x=*static_cast<Lease*>(p);++x.captures;return x.envelope_ok;},
        [](void* p){auto& x=*static_cast<Lease*>(p);++x.restores;return x.restore_ok;}};}
    ~Lease(){// Host fixture is torn down only after actual completion or no work.
        if(a)a->Release();if(b)b->Release();}
};
struct Consumer {
    Rig* rig=nullptr;ID3D12Resource* output=nullptr;ID3D12Resource* expected=nullptr;
    unsigned calls=0;Result result=1;bool copy=true;Lease* invalidate=nullptr;
    static Result Call(void* p,ID3D12GraphicsCommandList* l,Parameters params){
        auto& c=*static_cast<Consumer*>(p);++c.calls;void* raw=nullptr;
        auto r=params.read(params.object,"Color",true,&raw);
        if(r!=1)r=params.read(params.object,"Color",false,&raw);
        Need(r==1&&raw,"consumer Color");
        if(c.expected)Need(raw==c.expected,"consumer parameter identity");
        if(c.copy){auto* color=static_cast<ID3D12Resource*>(raw);
            Barrier(l,color,read,D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(l,c.output,read,D3D12_RESOURCE_STATE_COPY_DEST);
            l->CopyResource(c.output,color);
            Barrier(l,c.output,D3D12_RESOURCE_STATE_COPY_DEST,read);
            Barrier(l,color,D3D12_RESOURCE_STATE_COPY_SOURCE,read);}
        if(c.invalidate)c.invalidate->current_ok=false;
        return c.result;
    }
};
Frame Make(Rig& h,ID3D12Resource* color,unsigned format){
    Frame f{};f.recording={1,1,reinterpret_cast<Id>(h.list.p),h.serial,h.serial};f.source={2,3,h.serial};
    f.view=4;f.module_generation=5;f.feature_generation=6;f.color={color,format,0,read,true};
    f.render_width=9;f.render_height=5;f.output_width=23;f.output_height=13;f.base_x=3;f.base_y=2;
    f.native_contract_known=f.transparency_absent_known=f.all_color_aliases_known=f.single_layer=f.fg_off_known=true;
    f.pre_exposure=2.5f;f.exposure_scale=.625f;return f;
}
void Pixels(Rig& h,unsigned format){
    Com<ID3D12Resource> color,out; color.p=h.Texture(format);out.p=h.Texture(format);
    const auto desc=color->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
    h.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
    Com<ID3D12Resource> upload,readback;upload.p=h.Buffer(size,true);readback.p=h.Buffer(size,false);
    void* mapped=nullptr;Hr(upload->Map(0,nullptr,&mapped));std::memset(mapped,0,size);
    const unsigned bytes=format==10?8:16;std::vector<unsigned char> expected(17*11*bytes);
    // Arbitrary stored bit patterns include signed/zero/subnormal/alpha bits.
    // No arithmetic, quantization or exposure normalization is allowed.
    for(unsigned y=0;y<11;++y)for(unsigned x=0;x<17*bytes;++x){
        const auto v=static_cast<unsigned char>((x*37+y*61+17)&255);
        expected[y*17*bytes+x]=v;static_cast<unsigned char*>(mapped)[y*fp.Footprint.RowPitch+x]=v;}
    upload->Unmap(0,nullptr);Barrier(h.list,color,read,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    to.pResource=color;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Barrier(h.list,color,D3D12_RESOURCE_STATE_COPY_DEST,read);
    Lease lease{};lease.rig=&h;lease.current=Make(h,color,format);
    auto* c=Create(h.device,lease.Api());Need(c!=nullptr,"cold context");
    Params p;p.original=color.p;p.typed["Color"]=color.p;p.untyped["Color"]=color.p;
    p.typed["Output"]=out.p;const auto original_t=p.typed,original_u=p.untyped;
    Consumer native{&h,out.p};
    const auto result=Evaluate(c,true,h.list,lease.current,p.Api(),lease.State(),64ull<<20,Consumer::Call,&native);
    Need(result.native_calls==1&&native.calls==1&&result.copied&&result.substituted&&result.restored,"identity exactly once");
    Need(p.typed==original_t&&p.untyped==original_u,"all stored parameter keys restored");
    Need(!Retire(c),"unsubmitted recording cannot retire");
    Barrier(h.list,out,read,D3D12_RESOURCE_STATE_COPY_SOURCE);
    from={};to={};from.pResource=out;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
    h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);h.Submit();lease.submitted=true;
    Need(!Retire(c),"completed fence does not release replayable recording");
    lease.released=true;lease.completed_visible=false;
    Need(!Retire(c),"released recording does not replace completion proof");lease.completed_visible=true;
    Hr(readback->Map(0,nullptr,&mapped));
    for(unsigned y=0;y<11;++y)Need(std::memcmp(static_cast<unsigned char*>(mapped)+fp.Offset+y*fp.Footprint.RowPitch,
        expected.data()+y*17*bytes,17*bytes)==0,"entire physical copy exact including alpha/outside nonzero subrect");
    readback->Unmap(0,nullptr);Need(Retire(c)&&!c&&lease.retirements==1,"dual proof retires once");h.Reset();
}
void Declines(Rig& h){
    Com<ID3D12Resource> color;color.p=h.Texture(10);
    for(unsigned kind=0;kind<18;++kind){
        Lease lease{};lease.rig=&h;lease.current=Make(h,color,10);Frame f=lease.current;
        Params p;p.original=color.p;p.typed["Color"]=color.p;Consumer native{&h,nullptr,color.p,0,1,false};
        bool enabled=true;std::uint64_t budget=64ull<<20;
        switch(kind){
        case 0:enabled=false;break;case 1:f.native_contract_known=false;break;
        case 2:f.transparency_absent_known=false;break;case 3:f.all_color_aliases_known=false;break;
        case 4:f.fg_off_known=false;break;case 5:f.pre_exposure=0;break;
        case 6:++f.recording.queue;break;case 7:++f.recording.lifetime;break;
        case 8:++f.module_generation;break;case 9:++f.feature_generation;break;
        case 10:p.typed["DLSSD.ColorBeforeTransparency"]=color.p;break;
        case 11:p.typed["Depth"]=color.p;break;case 12:f.color.state_known=false;break;
        case 13:f.color.format=2;break;case 14:f.base_x=17;break;
        case 15:budget=0;break;case 16:lease.envelope_ok=false;break;case 17:lease.record_ok=false;break;
        }
        const auto old_t=p.typed,old_u=p.untyped;auto* c=Create(h.device,lease.Api());
        auto result=Evaluate(c,enabled,h.list,f,p.Api(),lease.State(),budget,Consumer::Call,&native);
        Need(result.native_calls==1&&native.calls==1&&!result.copied&&!result.substituted,"decline native once unchanged");
        Need(p.typed==old_t&&p.untyped==old_u,"decline does not insert missing parameter keys");
        if(!enabled)Need(lease.queries==0&&lease.records==0&&lease.captures==0,"disabled zero adapter/GPU work");
        Need(Retire(c),"cold decline retires");
    }
}
void Faults(Rig& h){
    for(unsigned kind=0;kind<7;++kind){
        Com<ID3D12Resource> color;color.p=h.Texture(10);
        Lease lease{};lease.rig=&h;lease.current=Make(h,color,10);
        auto* c=Create(h.device,lease.Api());Params p;p.original=color.p;p.typed["Color"]=color.p;
        Consumer native{&h,nullptr,nullptr,0,1,false};
        switch(kind){
        case 0:p.drop_write=true;native.expected=color.p;break;
        case 1:p.unified=false;p.untyped["Color"]=color.p;native.expected=color.p;break;
        case 2:p.break_restore=true;break;
        case 3:native.result=0xBAD0000Fu;break;
        case 4:native.invalidate=&lease;break;
        case 5:lease.restore_ok=false;break;
        case 6:lease.mark_ok=false;break;
        }
        const auto t=p.typed,u=p.untyped;
        const auto r=Evaluate(c,true,h.list,lease.current,p.Api(),lease.State(),64ull<<20,Consumer::Call,&native);
        Need(r.copied,"fault reached actual copy recording");
        if(kind<2){
            Need(r.native_calls==1&&native.calls==1&&!r.substituted&&r.restored,"failed substitution restores before one baseline call");
            Need(p.typed==t&&p.untyped==u,"dual getter/existence restored exactly");
        }else if(kind<5){
            Need(r.native_calls==1&&native.calls==1,"opaque native failure never retries");
            Need(lease.native_started&&lease.poisoned,"opaque consumer marked before entry and quarantined");
            if(kind==2)Need(!r.restored&&r.reason==Reason::RestoreFailed,"restore fault is fatal");
            if(kind==3)Need(r.result==native.result&&r.restored,"native failure result retained with CPU parameters restored");
            if(kind==4)Need(r.reason==Reason::OpaqueFailure&&r.restored,"generation drift quarantines without retry");
        }else{
            Need(r.native_calls==0&&native.calls==0&&lease.poisoned,"pre-native fatal does not call unverifiable native");
            if(kind==6)Need(p.typed==t&&p.untyped==u,"failed consumer mark restores Color");
        }
        Need(!Retire(c),"no unsubmitted/copy-only implicit retirement");
        h.Submit();lease.submitted=lease.released=true;
        if(kind<2)Need(Retire(c),"ordinary decline lease safely retires");
        else{
            Need(!Retire(c),"completion cannot release poisoned opaque resources");
            const auto off=Evaluate(c,false,h.list,lease.current,p.Api(),lease.State(),0,Consumer::Call,&native);
            Need(off.native_calls==0&&off.reason==Reason::OpaqueFailure,"switching off cannot escape quarantine");
            // Deliberately retain poisoned Context until process exit. No forced
            // release, timeout, or reset is used as an opaque retirement proof.
        }
        h.Reset();
    }
}
void FormatFlip(Rig& h){
    Lease lease{};lease.rig=&h;auto* c=Create(h.device,lease.Api());
    for(unsigned format:{10u,2u,10u}){
        Com<ID3D12Resource> color;color.p=h.Texture(format);lease.current=Make(h,color,format);
        Params p;p.original=color.p;p.typed["Color"]=color.p;
        Consumer native{&h,nullptr,nullptr,0,1,false};
        const auto r=Evaluate(c,true,h.list,lease.current,p.Api(),lease.State(),64ull<<20,Consumer::Call,&native);
        Need(r.copied&&r.substituted&&r.native_calls==1&&r.restored,"same context same extent format flip accepted exactly once");
        Need(lease.b->GetDesc().Format==color->GetDesc().Format,"format flip replaces complete physical descriptor");
        Need(!Retire(c),"new format recording is not released by old lease proof");
        h.Submit();lease.submitted=true;
        Need(!Retire(c),"new format fence completion still needs its own recording release");
        lease.released=true;h.Reset();
    }
    Need(Retire(c)&&lease.records==3&&lease.retirements==3,"format flip acquires and releases three exact leases");
}
}
int main(){try{Rig h;Declines(h);Pixels(h,10);Pixels(h,2);FormatFlip(h);Faults(h);
    std::printf("RR identity WARP: %u assertions passed; native callback=controlled copy; NGX=not run; game=not run; envelope=controlled, production adapter pending\n",checks);
    return 0;}catch(const std::exception& e){std::fprintf(stderr,"RR identity failed: %s\n",e.what());return 41;}}
