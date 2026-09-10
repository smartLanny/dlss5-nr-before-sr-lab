#pragma once
#include <cstdint>

// Value descriptions of the views we actually write. Equality is exact; a
// hash collision can never authorize reusing an in-flight descriptor table.
// The Core's views are either one-mip Texture2D SRV/UAV or raw buffer UAV.
namespace nr_binding {
constexpr unsigned kDescriptors = 24, kHeaps = 4, kPackets = 8;
enum Heap : unsigned { Codec, Consumer, Region, Depth };
enum class ViewKind : unsigned { Empty, Srv2D, Uav2D, RawUav };
struct View {
    ID3D12Resource* resource = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ViewKind kind = ViewKind::Empty;
    unsigned elements = 0;
    bool operator==(const View& b) const noexcept {
        return resource==b.resource && format==b.format && kind==b.kind && elements==b.elements;
    }
};
struct DescriptorSet {
    View views[kDescriptors]{};
    unsigned count = 0;
    bool valid = true;
    void Put(unsigned i,ID3D12Resource* r,DXGI_FORMAT f,ViewKind k,unsigned n=0) noexcept {
        if(i>=kDescriptors){valid=false;return;}
        views[i]={r,f,k,n};if(count<=i)count=i+1;
    }
    void Srv(unsigned i,ID3D12Resource* r,DXGI_FORMAT f) noexcept {Put(i,r,f,ViewKind::Srv2D);}
    void Uav(unsigned i,ID3D12Resource* r,DXGI_FORMAT f) noexcept {Put(i,r,f,ViewKind::Uav2D);}
    void Raw(unsigned i,ID3D12Resource* r,unsigned n) noexcept {Put(i,r,DXGI_FORMAT_R32_TYPELESS,ViewKind::RawUav,n);}
    bool operator==(const DescriptorSet& b) const noexcept {
        if(valid!=b.valid || count!=b.count)return false;
        for(unsigned i=0;i<count;++i)if(!(views[i]==b.views[i]))return false;
        return true;
    }
};
struct Plan {
    DescriptorSet heaps[kHeaps]{};
    // These owners participate in the same exact recording/fence ledger as
    // the descriptor lease. They are also used before replacing owned planes.
    const void* primary_owner = nullptr;
    const void* secondary_owner = nullptr;
    ID3D12Resource* retained[2]{}; // NR guide inputs, including formats with no shader view.
    bool Valid() const noexcept {
        bool any=false;
        for(const auto& h:heaps){if(!h.valid)return false;any|=h.count!=0;}
        return any && primary_owner;
    }
    bool operator==(const Plan& b) const noexcept {
        if(primary_owner!=b.primary_owner || secondary_owner!=b.secondary_owner)return false;
        for(unsigned i=0;i<2;++i)if(retained[i]!=b.retained[i])return false;
        for(unsigned i=0;i<kHeaps;++i)if(!(heaps[i]==b.heaps[i]))return false;
        return true;
    }
};
}
