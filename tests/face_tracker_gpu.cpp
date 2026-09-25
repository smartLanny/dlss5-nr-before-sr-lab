// Reuse the existing real WARP device/upload helper; its test main is not run.
#include <memory>
#define main replay_fixture_unused_main
#include "replay_gpu.cpp"
#undef main
#include "../src/nr_face_tracker_gpu.inl"

template<class T>std::vector<unsigned char> Bytes(const std::vector<T>& input) {
    std::vector<unsigned char> out(input.size()*sizeof(T));std::memcpy(out.data(),input.data(),out.size());return out;
}
float Half(std::uint16_t v) {
    const unsigned exponent=(v>>10)&31,mantissa=v&1023;
    const float sign=v&0x8000?-1.f:1.f;
    return exponent?sign*std::ldexp(1.f+mantissa/1024.f,int(exponent)-15):sign*std::ldexp(float(mantissa),-24);
}
std::vector<float> MaskPixels(Gpu& gpu,ID3D12Resource* image) {
    const auto d=ID3D12Resource_GetDesc(image);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    ID3D12Device_GetCopyableFootprints(gpu.device,&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=1;b.DepthOrArraySize=1;
    b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;ID3D12Resource* readback=nullptr;
    CheckHr(ID3D12Device_CreateCommittedResource(gpu.device,&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&readback)),"mask readback");
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    ID3D12GraphicsCommandList_ResourceBarrier(gpu.list,1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(gpu.list,&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);ID3D12GraphicsCommandList_ResourceBarrier(gpu.list,1,&barrier);
    gpu.Execute();void* data=nullptr;const D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};
    CheckHr(ID3D12Resource_Map(readback,0,&range,&data),"map real face mask");
    std::vector<float> pixels(static_cast<std::size_t>(d.Width)*d.Height);
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x) {
        std::uint16_t value{};std::memcpy(&value,static_cast<unsigned char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*8,2);
        pixels[y*d.Width+x]=Half(value);
    }
    const D3D12_RANGE empty{0,0};ID3D12Resource_Unmap(readback,0,&empty);ReleaseCom(readback);return pixels;
}
int main() {
    using namespace nr_face_gpu;
    Gpu gpu;Tracker tracker;Check(tracker.Ensure(gpu.device),"real tracker pipelines compile/create");
    constexpr unsigned w=64,h=64;
    std::vector<float> proxy(w*h*4),motion(w*h*2),depth(w*h,.4f),occluder(w*h,.7f);
    for(unsigned p=0;p<w*h;++p){proxy[p*4]=.25f;proxy[p*4+1]=.5f;proxy[p*4+2]=.75f;proxy[p*4+3]=1;motion[p*2]=.125f;}
    Inputs inputs{};inputs.proxy=gpu.Texture(DXGI_FORMAT_R32G32B32A32_FLOAT,w,h,16,Bytes(proxy));
    inputs.motion=gpu.Texture(DXGI_FORMAT_R32G32_FLOAT,w,h,8,Bytes(motion));inputs.depth=gpu.Texture(DXGI_FORMAT_R32_FLOAT,w,h,4,Bytes(depth));
    inputs.proxy_format=DXGI_FORMAT_R32G32B32A32_FLOAT;inputs.motion_format=DXGI_FORMAT_R32G32_FLOAT;
    auto* blocked=gpu.Texture(DXGI_FORMAT_R32_FLOAT,w,h,4,Bytes(occluder));
    nr_region::Frame identity{1,1,1,1920,1080,0,0,1920,1080,nr_region::Route::BeforeSr,nr_region::ColorDomain::Encoded709,1};
    Constants c{};c.width=w;c.height=h;c.proxy_width=w;c.proxy_height=h;c.motion_width=w;c.motion_height=h;c.depth_width=w;c.depth_height=h;
    c.motion_scale_x=c.motion_scale_y=1;c.face_count=1;c.depth_valid=c.motion_valid=1;
    Face face{{.2f,.2f,.6f,.6f,.35f,.4f,.65f,.4f,.5f,.5f,.45f,.6f,.55f,.6f,.9f,0}};
    auto ready=[](void* q,std::uint64_t f){return ID3D12Fence_GetCompletedValue(static_cast<ID3D12Fence*>(q))>=f;};
    unsigned captures=0;
    auto consume=[&](const nr_region::Frame& frame,ID3D12Resource* resource,unsigned width,unsigned height) {
        ++captures;Check(frame.context==1&&frame.epoch==1,"readback identity");
        void* data=nullptr;const D3D12_RANGE range{0,width*height*4ull+16};CheckHr(ID3D12Resource_Map(resource,0,&range,&data),"reference map");
        const auto* packed=static_cast<const std::uint32_t*>(data);
        for(unsigned i=0;i<width*height;++i)Check(packed[i]==(191u|(128u<<8)|(64u<<16)),"complete before-NR BGR readback");
        const D3D12_RANGE none{0,0};ID3D12Resource_Unmap(resource,0,&none);
    };
    const auto first=tracker.Arm(identity,w,h);Check(first<Tracker::Invalid,"first arm");
    Check(tracker.Record(first,gpu.list,100,inputs,c,&face),"first guide and mask record");
    auto previous=tracker.Commit(first);Check(previous&&previous->valid,"only successful evaluation publishes guide");
    tracker.Submit(100,gpu.fence,gpu.next);gpu.Execute();
    tracker.Collect(ready,consume);Check(captures==0,"completed fence alone does not retire recording");
    const auto first_mask=MaskPixels(gpu,tracker.Mask(first));Check(first_mask[32*w+32]>.99f,"face centre covered");Check(first_mask[0]==0,"outside face untouched");
    tracker.Invalidate(100,false);tracker.Collect(ready,consume);Check(captures==1,"reference exported after exact ownership release");
    identity.frame=2;inputs.history[0]=previous;c.face_age=1;c.has_previous=1;
    const auto second=tracker.Arm(identity,w,h);Check(second!=first,"immutable history blocks resource reuse");
    Check(tracker.Record(second,gpu.list,101,inputs,c,&face),"one-step temporal reprojection");
    auto second_guide=tracker.Commit(second);tracker.Submit(101,gpu.fence,gpu.next);gpu.Execute();
    const auto second_mask=MaskPixels(gpu,tracker.Mask(second));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w-8;++x)Check(std::abs(second_mask[y*w+x]-first_mask[y*w+x+8])<.002f,"current-to-previous direction matches independent translated image");
    tracker.Invalidate(101,false);tracker.Collect(ready,consume);
    identity.frame=3;inputs.history[1]=previous;inputs.history[0]=second_guide;auto* normal_depth=inputs.depth;inputs.depth=blocked;c.face_age=2;
    const auto third=tracker.Arm(identity,w,h);Check(tracker.Record(third,gpu.list,102,inputs,c,&face),"occlusion record");
    tracker.Commit(third);tracker.Submit(102,gpu.fence,gpu.next);gpu.Execute();
    for(float value:MaskPixels(gpu,tracker.Mask(third)))Check(value==0,"depth discontinuity exits protection immediately");
    tracker.Invalidate(102,false);tracker.Collect(ready,consume);inputs.depth=normal_depth;
    identity.epoch=2;identity.frame=4;const auto mismatch=tracker.Arm(identity,w,h);
    Check(!tracker.Record(mismatch,gpu.list,103,inputs,c,&face),"old epoch history refused before recording");tracker.Discard(mismatch);
    identity.epoch=1;c.face_age=9;const auto stale=tracker.Arm(identity,w,h);
    Check(!tracker.Record(stale,gpu.list,104,inputs,c,&face),"excess history age rejected");tracker.Discard(stale);
    c.face_age=0;c.has_previous=0;c.reset=1;
    const auto reset=tracker.Arm(identity,w,h);Check(tracker.Record(reset,gpu.list,105,inputs,c,&face),"reset mask recorded");
    tracker.Commit(reset);tracker.Submit(105,gpu.fence,gpu.next);gpu.Execute();
    for(float value:MaskPixels(gpu,tracker.Mask(reset)))Check(value==0,"reset never retains prior face");
    tracker.Invalidate(105,false);tracker.Collect(ready,consume);
    // A proven-unsubmitted list is invalid, even if CPU evaluation succeeded.
    c.reset=0;identity.frame=5;const auto unsubmitted=tracker.Arm(identity,w,h);
    Check(tracker.Record(unsubmitted,gpu.list,106,inputs,c,&face),"unsubmitted recording");auto invalid=tracker.Commit(unsubmitted);
    CheckHr(ID3D12GraphicsCommandList_Close(gpu.list),"unsubmitted close");
    CheckHr(ID3D12CommandAllocator_Reset(gpu.allocator),"unsubmitted allocator reset");
    CheckHr(ID3D12GraphicsCommandList_Reset(gpu.list,gpu.allocator,nullptr),"unsubmitted list reset");
    tracker.Invalidate(106,true);tracker.Collect(ready,consume);Check(!invalid->valid,"unsubmitted history invalidated");
    Check(tracker.Empty(),"all exact recording leases drained");
    for(auto& history:inputs.history)history.reset();previous.reset();second_guide.reset();invalid.reset();
    Check(tracker.ResetDevice(),"safe tracker shutdown");Check(allocated_bytes.load()==0,"actual resource allocation budget drained");
    ReleaseCom(inputs.proxy);ReleaseCom(inputs.motion);ReleaseCom(inputs.depth);ReleaseCom(blocked);
    std::printf("Face tracker real WARP: %u checks PASS; synthetic motion/depth, no semantic/game/performance acceptance\n",checks);return 0;
}
