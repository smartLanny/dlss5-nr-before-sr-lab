#include "src/nr_descriptor_provenance.h"
#include "src/nr_post_boundary.h"
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace nr_descriptor;
std::atomic<unsigned> checks{0};
void Check(bool value,const char *message) {
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(41);}
}
using TestRegistry=Registry<16,16,128>;
Resource output{1,7,0x5000},other{2,7,0x6000};
Heap cpu{1,7,0x100,0x1000,0,16,32,0,false};
Heap gpu{2,7,0x200,0x2000,0x3000,16,32,0,true};
View MakeView(Resource resource=output,unsigned format=28,Kind kind=Kind::Uav) {
    return {resource,kind,format,0,0,16,12,true,true,true};
}
void WriteView(TestRegistry &registry,Id destination,View view) {
    const auto write=registry.BeginWrite(destination,0,view);registry.Finish(write);
}
void Seed(TestRegistry &registry) {
    registry.ObserveResource(output);registry.ObserveResource(other);
    Check(registry.ObserveHeap(cpu)&&registry.ObserveHeap(gpu),"observed real heap ranges accepted");
    WriteView(registry,cpu.cpu,MakeView());
    const auto copy=registry.BeginCopy(gpu.cpu,cpu.cpu,0);registry.Finish(copy);
}
Pair ReadPair(TestRegistry &registry) {return registry.UavPair(7,output,gpu.id,gpu.gpu,cpu.cpu);}
void CachedUse() {
    TestRegistry registry;Seed(registry);
    const auto pair=ReadPair(registry);
    const auto inventory=registry.ResourceInventory(output.id);
    Check(inventory.lifetime_known&&inventory.slots==2&&inventory.uav==2&&inventory.count==1&&inventory.formats[0].format==28,
        "bounded cached-only inventory reports copied views without claiming native use");
    Check(static_cast<bool>(pair),"cached CPU descriptor copied to GPU resolves the same typed output");
    Check(pair.cpu.stamp.heap!=pair.gpu.stamp.heap,"clear CPU/nonvisible and GPU/visible heaps retain distinct lifetimes");
    Check(!registry.UavPair(8,output,gpu.id,gpu.gpu,cpu.cpu),"other device cannot borrow provenance");
    Check(!registry.UavPair(7,other,gpu.id,gpu.gpu,cpu.cpu),"explicit other resource cannot be called Output");
    Check(!registry.UavPair(7,output,999,gpu.gpu,cpu.cpu),"GPU view must be in currently bound heap");
    Check(!registry.UavPair(7,output,gpu.id,gpu.gpu,gpu.cpu),"CPU clear handle in a shader-visible heap is not accepted");
    Check(!registry.Cpu(cpu.cpu+1,0),"misaligned opaque handle is unknown");
    Check(!registry.Cpu(cpu.cpu+16*32,0),"handle outside observed heap is unknown");
    NativeUse absent;absent.hooks_ready=absent.identity_known=true;absent.Finish(true,registry);
    Check(!absent.observed_typed&&!absent.after_native_valid,"creating a UNORM descriptor proves no actual native use");
    NativeUse use;use.hooks_ready=use.identity_known=true;use.Observe(pair);use.Finish(true,registry);
    Check(use.observed_typed==28&&use.after_native_valid,"explicit native clear can report typed observation");
    WriteView(registry,gpu.cpu,MakeView(other));
    use.Submit(1,2,1,3,registry);
    Check(!use.at_submit_valid&&use.submission_index==1&&use.submission_count==3,
        "volatile overwrite after native return invalidates Execute-entry receipt");
    use.Submit(1,3,0,1,registry);
    Check(use.replayed&&use.submission==2,"repeat Execute preserves first receipt and reports replay");
    WriteView(registry,gpu.cpu,MakeView());
    NativeUse failed;failed.hooks_ready=failed.identity_known=true;failed.Observe(ReadPair(registry));failed.Finish(false,registry);
    Check(!failed.after_native_valid,"failed native Evaluate cannot supply a successful use receipt");
    NativeUse overflow;overflow.hooks_ready=overflow.identity_known=true;
    for(unsigned i=0;i<5;++i)overflow.Observe(ReadPair(registry));
    overflow.Finish(true,registry);
    Check(overflow.count==4&&overflow.overflow&&!overflow.after_native_valid,"bounded use overflow is an explicit gap");
    NativeUse conflict;conflict.hooks_ready=conflict.identity_known=true;conflict.Observe(ReadPair(registry));
    WriteView(registry,cpu.cpu,MakeView(output,30));WriteView(registry,gpu.cpu,MakeView(output,30));
    conflict.Observe(ReadPair(registry));conflict.Finish(true,registry);
    Check(conflict.conflict&&!conflict.after_native_valid,"different real typed views remain ambiguous");
}
void CopiesAndOverwrites() {
    TestRegistry registry;Seed(registry);
    const auto first=registry.Cpu(cpu.cpu,0);
    const auto copy=registry.BeginCopy(gpu.cpu,cpu.cpu,0);
    WriteView(registry,cpu.cpu,MakeView(output,30));registry.Finish(copy);
    Check(!registry.Gpu(gpu.gpu,0),"copy source overwritten during native copy loses evidence");
    Check(!registry.Current(first),"revision receipt is not revived by matching resource identity");
    const auto known_copy=registry.BeginCopy(gpu.cpu,cpu.cpu,0);registry.Finish(known_copy);
    Check(registry.Gpu(gpu.gpu,0).view.format==30,"new complete copy can recover the actual UINT interpretation");
    const auto missing_copy=registry.BeginCopy(gpu.cpu,cpu.cpu+32,0);registry.Finish(missing_copy);
    Check(!registry.Gpu(gpu.gpu,0),"copying an untracked source invalidates old destination evidence");
    WriteView(registry,gpu.cpu,MakeView());
    const auto overwrite=registry.BeginWrite(gpu.cpu,0,{});registry.Finish(overwrite);
    Check(!registry.Gpu(gpu.gpu,0),"CBV/null/unknown descriptor overwrite cannot retain previous UAV");
    WriteView(registry,cpu.cpu,MakeView());
    const auto a=registry.BeginWrite(cpu.cpu,0,MakeView());
    const auto b=registry.BeginWrite(cpu.cpu,0,MakeView(output,30));
    registry.Finish(b);registry.Finish(a);
    Check(!registry.Cpu(cpu.cpu,0),"overlapping writes stay unknown regardless of callback completion order");
    WriteView(registry,cpu.cpu,MakeView());
    Check(static_cast<bool>(registry.Cpu(cpu.cpu,0)),"later uncontended complete write restores provenance");
    const auto pending=registry.BeginWrite(cpu.cpu,0,MakeView());registry.Invalidate();registry.Finish(pending);
    Check(!registry.Cpu(cpu.cpu,0),"capacity loss invalidates in-flight receipts too");
    Heap foreign=gpu;foreign.id=3;foreign.device=8;foreign.address+=32;foreign.cpu+=0x4000;foreign.gpu+=0x4000;
    registry.ObserveHeap(foreign);WriteView(registry,cpu.cpu,MakeView());
    const auto cross=registry.BeginCopy(foreign.cpu,cpu.cpu,0);registry.Finish(cross);
    Check(!registry.Gpu(foreign.gpu,0),"cross-device copied value is refused");
}
void LifetimesAndCapacity() {
    TestRegistry registry;Seed(registry);const auto old=ReadPair(registry);
    registry.RetireResource(output.id);
    Check(!registry.ResourceInventory(output.id).lifetime_known,"retired lifetime has no current cached inventory");
    Check(!registry.Current(old)&&!registry.Cpu(cpu.cpu,0),"resource destruction retires descriptors without dereferencing its address");
    const Resource reused{3,7,output.address};registry.ObserveResource(reused);
    Check(registry.ResourceAt(output.address).id==reused.id&&!registry.Cpu(cpu.cpu,0),"reused object address does not revive old resource identity");
    registry.ObserveResource(output);
    Check(!registry.ResourceAt(output.address),"two live tokens at one address are ambiguous");
    registry.RetireResource(reused.id);
    registry.RetireHeap(gpu.id);Heap replacement=gpu;replacement.id=4;registry.ObserveHeap(replacement);
    Check(!registry.Gpu(gpu.gpu,0)&&!registry.Current(old),"reused CPU/GPU heap ranges need fresh descriptor writes");
    registry.RetireDevice(7);
    Check(!registry.HeapAt(cpu.address)&&!registry.ResourceAt(output.address),"device retirement invalidates all its value records");
    Registry<2,2,2> small;small.ObserveResource(output);small.ObserveHeap(cpu);
    auto write=small.BeginWrite(cpu.cpu,0,MakeView());small.Finish(write);
    const auto receipt=small.Cpu(cpu.cpu,0);
    write=small.BeginWrite(cpu.cpu+64,0,MakeView());small.Finish(write);
    Check(!small.Current(receipt),"bounded slot collision loses evidence instead of aliasing another slot");
    small.ObserveResource({3,7,0x7000});
    Check(!small.Cpu(cpu.cpu+64,0),"bounded resource eviction invalidates referencing slots");
    Heap overlap=cpu;overlap.id=2;overlap.address+=1;small.ObserveHeap(overlap);
    Check(!small.Cpu(cpu.cpu,0),"overlapping live heap ranges do not pick an arbitrary owner");
    Heap invalid=cpu;invalid.cpu=~Id{0}-4;
    Check(!small.ObserveHeap(invalid),"heap handle arithmetic cannot wrap");
}
void ConcurrentWrites() {
    TestRegistry registry;Seed(registry);
    std::barrier gate(2);
    std::thread first([&] {
        for(unsigned i=0;i<256;++i) {
            const auto a=registry.BeginWrite(cpu.cpu,0,MakeView());
            gate.arrive_and_wait();gate.arrive_and_wait();registry.Finish(a);gate.arrive_and_wait();
        }
    });
    std::thread second([&] {
        for(unsigned i=0;i<256;++i) {
            gate.arrive_and_wait();
            Check(!registry.Cpu(cpu.cpu,0),"concurrent reader never consumes an unfinished native writer");
            const auto b=registry.BeginWrite(cpu.cpu,0,MakeView(output,30));registry.Finish(b);
            gate.arrive_and_wait();gate.arrive_and_wait();
            Check(!registry.Cpu(cpu.cpu,0),"concurrent completion cannot make overlapping writes authoritative");
        }
    });
    first.join();second.join();
    Check(registry.SnapshotCounters().overlaps==256,"all forced overlap races are reported exactly once");
}
struct Rect {std::int32_t left,top,right,bottom;};
void ClearExtents() {
    auto view=MakeView();
    const auto full=DescribeClear<Rect>(view,0,nullptr);
    Check(full.kind==RangeKind::FullView&&full.bounds[2]==16&&full.bounds[3]==12,"zero rectangles records whole observed view");
    Rect rectangles[]={{2,3,6,8},{10,2,14,5}};
    const auto one=DescribeClear(view,1,rectangles),many=DescribeClear(view,2,rectangles);
    Check(one.kind==RangeKind::SingleRect&&one.bounds[0]==2&&one.bounds[3]==8,"one rectangle retains exact extent");
    Check(many.kind==RangeKind::RectEnvelope&&many.bounds[0]==2&&many.bounds[1]==2&&many.bounds[2]==14,
        "multiple rectangles report their envelope, never invented full coverage");
    Check(DescribeClear<Rect>(view,1,nullptr).kind==RangeKind::Unknown,"missing rectangle memory is unknown");
    Check(DescribeClear(view,65,rectangles).kind==RangeKind::Unknown,"oversized rectangle input is not read beyond the bound");
    rectangles[0].right=17;
    Check(DescribeClear(view,1,rectangles).kind==RangeKind::Unknown,"out-of-view rectangle does not invent clipping semantics");
    view.shape_known=false;
    Check(DescribeClear<Rect>(view,0,nullptr).kind==RangeKind::Unknown,"unknown view shape cannot be labeled full Output");
    Check(sizeof(nr_post_boundary::Window)<4096,"full event windows retain the small worker-stack budget");
}
int main() {
    CachedUse();CopiesAndOverwrites();LifetimesAndCapacity();ConcurrentWrites();ClearExtents();
    std::printf("PASS descriptor provenance CPU: %u checks; production registry, copy/reuse/retirement/concurrency/range negatives; no GPU\n",checks.load());
}
