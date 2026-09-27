// CPU-only contract test for the exact private NGX descriptor-heap adapter.
#define CINTERFACE
#define COBJMACROS
#include <d3d12.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "nr_private_descriptor_heaps.h"

namespace {
constexpr GUID kUnwrapped = {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
struct Heap {
    ID3D12DescriptorHeap iface{};
    ID3D12DescriptorHeapVtbl vtable{};
    unsigned refs=1, queries=0, releases=0;
    Heap* original=nullptr;
    explicit Heap(Heap* raw=nullptr):original(raw) {
        vtable.QueryInterface=Query;
        vtable.AddRef=Add;
        vtable.Release=Release;
        iface.lpVtbl=&vtable;
    }
    static Heap& Of(ID3D12DescriptorHeap* value) {return *reinterpret_cast<Heap*>(value);}
    static HRESULT STDMETHODCALLTYPE Query(ID3D12DescriptorHeap* self,REFIID iid,void** out) {
        auto& heap=Of(self);++heap.queries;
        if(out)*out=nullptr;
        if(heap.original&&std::memcmp(&iid,&kUnwrapped,sizeof(GUID))==0&&out){
            heap.original->iface.lpVtbl->AddRef(&heap.original->iface);
            *out=&heap.original->iface;
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    static ULONG STDMETHODCALLTYPE Add(ID3D12DescriptorHeap* self) {return ++Of(self).refs;}
    static ULONG STDMETHODCALLTYPE Release(ID3D12DescriptorHeap* self) {
        auto& heap=Of(self);++heap.releases;assert(heap.refs>0);return --heap.refs;
    }
};
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
void Forward(ID3D12DescriptorHeap* const* actual,UINT count,ID3D12DescriptorHeap* a,ID3D12DescriptorHeap* b=nullptr){
    Check(actual!=nullptr||count==0,"forward array");
    if(count>0)Check(actual[0]==a,"forward first heap");
    if(count>1)Check(actual[1]==b,"forward second heap");
}
}
int main(){
    Heap raw_a,raw_b,proxy_a(&raw_a),proxy_b(&raw_b);
    ID3D12DescriptorHeap* pair[]={&proxy_a.iface,&raw_b.iface};
    {
        ScopedPrivateDescriptorHeaps scoped(true,2,pair);
        Forward(scoped.data(),2,&raw_a.iface,&raw_b.iface);
        Check(raw_a.refs==2&&raw_b.refs==1,"private temporary reference stays live through forwarding");
        Check(proxy_a.queries==1&&raw_b.queries==1,"both heap domains examined once");
    }
    Check(raw_a.refs==1&&raw_a.releases==1,"unwrapped temporary reference released");
    Check(raw_b.refs==1&&raw_b.releases==0,"raw heap unchanged");
    {
        ScopedPrivateDescriptorHeaps scoped(false,2,pair);
        Forward(scoped.data(),2,&proxy_a.iface,&raw_b.iface);
    }
    Check(proxy_a.queries==1&&raw_b.queries==1,"nonprivate does not query or alter");
    ID3D12DescriptorHeap* both[]={&proxy_a.iface,&proxy_b.iface};
    {
        ScopedPrivateDescriptorHeaps scoped(true,2,both);
        Forward(scoped.data(),2,&raw_a.iface,&raw_b.iface);
        Check(raw_a.refs==2&&raw_b.refs==2,"two proxy references live through forward");
    }
    Check(raw_a.refs==1&&raw_b.refs==1,"two proxy references released");
    {
        ScopedPrivateDescriptorHeaps scoped(true,0,nullptr);
        Forward(scoped.data(),0,nullptr);
    }
    {
        ID3D12DescriptorHeap* empty[]={nullptr};
        ScopedPrivateDescriptorHeaps scoped(true,1,empty);
        Forward(scoped.data(),1,nullptr);
    }
    std::printf("private descriptor heap checks=%u\n",checks);
}
