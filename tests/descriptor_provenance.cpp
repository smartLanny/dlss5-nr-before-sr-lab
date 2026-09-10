#include "src/nr_descriptor_provenance.h"
#include "src/nr_post_boundary.h"
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include "descriptor-use-snapshot.inl"

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
    Check(small.Current(receipt),"two distinct slots survive a former direct-map collision while space remains");
    write=small.BeginWrite(cpu.cpu+32,0,MakeView());small.Finish(write);
    Check(!small.Current(receipt),"actual full CPU pool evicts the oldest completed slot and revokes its receipt");
    small.ObserveResource({3,7,0x7000});
    Check(static_cast<bool>(small.Cpu(cpu.cpu+64,0)),"second live resource cannot evict a different complete key");
    Heap overlap=cpu;overlap.id=2;overlap.address+=1;small.ObserveHeap(overlap);
    Check(!small.Cpu(cpu.cpu,0),"overlapping live heap ranges do not pick an arbitrary owner");
    Heap invalid=cpu;invalid.cpu=~Id{0}-4;
    Check(!small.ObserveHeap(invalid),"heap handle arithmetic cannot wrap");
}
void CompleteKeysAndPools() {
    auto registry=std::make_unique<Registry<>>();
    const Resource target{3448,7,0x5000};registry->ObserveResource(target);
    Heap original=cpu;original.id=10;original.count=8193;
    Heap remote=gpu;remote.id=11;remote.cpu=0x1000000;remote.gpu=0x2000000;
    Check(registry->ObserveHeap(original)&&registry->ObserveHeap(remote),"original counterexample heap pair registered");
    const auto put=[&](Id handle){const auto w=registry->BeginWrite(handle,0,MakeView(target));registry->Finish(w);};
    put(original.cpu);put(remote.cpu);
    const auto pair=registry->UavPair(7,target,remote.id,remote.gpu,original.cpu);
    Check(static_cast<bool>(pair),"original counterexample pair is known");
    registry->ObserveResource({3448+2048,7,0x9000});
    Check(registry->ResourceAt(target.address).id==target.id,"former 2048-modulus resource collision preserves Output");
    Check(registry->ResourceInventory(target.id).lifetime_known,"Output inventory retains its live resource");
    Check(registry->Current(pair),"unrelated resource registration preserves the paired view");
    Heap unrelated=cpu;unrelated.id=10+256;unrelated.address+=0x10000;unrelated.cpu=0x4000000;
    Check(registry->ObserveHeap(unrelated),"former 256-modulus heap collision registers a separate live key");
    Check(registry->Current(pair),"unrelated heap registration preserves the original range");
    put(original.cpu+8192*32);
    Check(registry->Current(pair),"same-heap slots 0 and 8192 both survive with free capacity");
    const auto counts=registry->SnapshotCounters();
    Check(!counts.evictions&&!counts.resources_retired&&!counts.heaps_retired,"collision repairs do not fabricate retirement or eviction");
    Check(counts.heaps.active==3&&counts.resources.active==2&&counts.cpu_slots.active==2&&counts.gpu_slots.active==1,
        "per-domain occupancy counts distinct complete keys");
    Check(sizeof(Registry<>)<=16u*1024u*1024u,"default bounded registry fits the 16 MiB static budget");
    std::printf("registry bytes: %zu\n",sizeof(Registry<>));

    Registry<4,4,2,4> split;split.ObserveResource(output);split.ObserveHeap(cpu);split.ObserveHeap(gpu);
    auto w=split.BeginWrite(cpu.cpu,0,MakeView());split.Finish(w);const auto source=split.Cpu(cpu.cpu,0);
    for(unsigned i=0;i<16;++i){w=split.BeginCopy(gpu.cpu+i*32,cpu.cpu,0);split.Finish(w);}
    const auto domains=split.SnapshotCounters();
    Check(split.Current(source),"GPU copy churn never evicts the persistent CPU source");
    Check(domains.cpu_slots.active==1&&!domains.cpu_slots.evictions&&domains.gpu_slots.active==4&&domains.gpu_slots.evictions==12,
        "GPU eviction is charged only at real GPU capacity and only to that pool");
    Check(split.Gpu(gpu.gpu+15*32,0).view.format==28,"GPU copy after repeated turnover still resolves its retained CPU source");

    Registry<2,2,2,2> lru;lru.ObserveResource(output);lru.ObserveHeap(cpu);lru.ObserveHeap(gpu);
    w=lru.BeginWrite(cpu.cpu,0,MakeView());lru.Finish(w);const auto hot=lru.Cpu(cpu.cpu,0);
    w=lru.BeginWrite(cpu.cpu+32,0,MakeView());lru.Finish(w);const auto cold=lru.Cpu(cpu.cpu+32,0);
    w=lru.BeginCopy(gpu.cpu,cpu.cpu,0);lru.Finish(w);
    w=lru.BeginWrite(cpu.cpu+64,0,MakeView());lru.Finish(w);
    Check(lru.Current(hot)&&!lru.Current(cold),"successful Copy use keeps a long-lived CPU source ahead of an unused newer descriptor");
    const auto cold_cpu=lru.Cpu(cpu.cpu+64,0),hot_gpu=lru.Gpu(gpu.gpu,0);
    w=lru.BeginWrite(gpu.cpu+32,0,MakeView());lru.Finish(w);const auto cold_gpu=lru.Gpu(gpu.gpu+32,0);
    Check(static_cast<bool>(lru.UavPair(7,output,gpu.id,gpu.gpu,cpu.cpu)),"explicit Clear pair can refresh both completed pools");
    w=lru.BeginWrite(cpu.cpu+96,0,MakeView());lru.Finish(w);
    w=lru.BeginWrite(gpu.cpu+64,0,MakeView());lru.Finish(w);
    Check(lru.Current(hot)&&lru.Current(hot_gpu)&&!lru.Current(cold_cpu)&&!lru.Current(cold_gpu),
        "matched explicit UAV use refreshes both CPU and GPU LRU without retaining unrelated slots");

    Registry<1,2,2,2> heap_full;heap_full.ObserveResource(output);heap_full.ObserveHeap(cpu);
    w=heap_full.BeginWrite(cpu.cpu,0,MakeView());heap_full.Finish(w);const auto before=heap_full.Cpu(cpu.cpu,0);
    Heap overlap=cpu;overlap.id=99;overlap.address+=1;
    Check(!heap_full.ObserveHeap(overlap),"full live heap pool drops new evidence instead of replacing a live heap");
    Check(!heap_full.Current(before)&&!heap_full.Cpu(cpu.cpu,0),"omitted overlapping heap makes existing uniqueness unknown");
    heap_full.RetireHeap(99);w=heap_full.BeginWrite(cpu.cpu,0,MakeView());heap_full.Finish(w);
    Check(!heap_full.Cpu(cpu.cpu,0),"retiring an omitted heap cannot revive pre-drop evidence");
    const auto heap_counts=heap_full.SnapshotCounters().heaps;
    Check(heap_counts.active==1&&heap_counts.capacity==1&&heap_counts.drops==1&&!heap_counts.evictions&&heap_counts.coverage_lost,
        "heap capacity loss is explicit and sticky");

    Registry<2,1,2,2> resource_full;resource_full.ObserveResource(output);resource_full.ObserveHeap(cpu);
    w=resource_full.BeginWrite(cpu.cpu,0,MakeView());resource_full.Finish(w);
    resource_full.ObserveResource({9,7,output.address});resource_full.RetireResource(9);
    Check(!resource_full.ResourceAt(output.address)&&!resource_full.Cpu(cpu.cpu,0),"dropped same-address resource never makes retained alias authoritative");
    const auto resource_counts=resource_full.SnapshotCounters().resources;
    Check(resource_counts.active==1&&resource_counts.capacity==1&&resource_counts.drops==1&&!resource_counts.evictions&&resource_counts.coverage_lost,
        "resource capacity loss is separate from actual retirement");

    Registry<2,4,4,4> ambiguous;ambiguous.ObserveResource(output);ambiguous.ObserveHeap(cpu);
    w=ambiguous.BeginWrite(cpu.cpu,0,MakeView());ambiguous.Finish(w);const auto unique=ambiguous.Cpu(cpu.cpu,0);
    ambiguous.ObserveResource({9,7,output.address});
    Check(!ambiguous.Current(unique),"second live token at the same address revokes the old receipt");
    w=ambiguous.BeginWrite(cpu.cpu,0,MakeView());ambiguous.Finish(w);
    Check(!ambiguous.Cpu(cpu.cpu,0),"new write cannot authorize an ambiguous live resource address");
    ambiguous.RetireResource(9);w=ambiguous.BeginWrite(cpu.cpu,0,MakeView());ambiguous.Finish(w);
    Check(!ambiguous.Current(unique)&&!ambiguous.Cpu(cpu.cpu,0),"retirement after address ambiguity cannot resurrect a stale interpretation");
    const auto ambiguity_counts=ambiguous.SnapshotCounters().resources;
    Check(ambiguity_counts.ambiguities==1&&!ambiguity_counts.drops&&ambiguity_counts.coverage_lost,
        "live-address ambiguity is counted separately from capacity drops");

    Registry<2,2,1,1> pending;pending.ObserveResource(output);pending.ObserveHeap(cpu);
    const auto a=pending.BeginWrite(cpu.cpu,0,MakeView());
    const auto lost=pending.BeginWrite(cpu.cpu+32,0,MakeView());pending.Finish(lost);
    Check(!lost.destination.heap&&pending.SnapshotCounters().cpu_slots.drops==1,"full pool never evicts an in-flight write");
    pending.Finish(a);Check(static_cast<bool>(pending.Cpu(cpu.cpu,0)),"original pending write completes after unrelated full-pool drop");
    const auto b=pending.BeginWrite(cpu.cpu,0,MakeView());pending.Invalidate();
    const auto c=pending.BeginWrite(cpu.cpu,0,MakeView(output,30));pending.Finish(c);pending.Finish(b);
    Check(!pending.Cpu(cpu.cpu,0),"writes overlapping across global invalidation remain unknown");
    w=pending.BeginWrite(cpu.cpu,0,MakeView());pending.Finish(w);
    Check(static_cast<bool>(pending.Cpu(cpu.cpu,0)),"all pre-invalidation pending writers retire without stranding the slot");

    LiveTable<Resource,32> churn;
    for(unsigned i=1;i<=32;++i)Check(churn.Insert({i,7,static_cast<Id>(i)*32}),"live table fills its object capacity with spare index buckets");
    for(unsigned i=1;i<=512;++i) {
        Check(churn.Erase(i),"retirement removes the exact live key despite repaired collisions");
        Check(!churn.Find(i)&&churn.Insert({i+32,7,static_cast<Id>(i+32)*32}),"high-occupancy deletion reuses capacity without tombstones");
    }
    Check(churn.active==32&&!churn.Insert({9999,7,0xffff}),"live-object cap remains exact even with spare index buckets");
    for(unsigned i=513;i<=544;++i)Check(churn.Find(i)!=nullptr,"every retained key survives repeated high-occupancy deletion");
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
void CallSnapshots() {
    Check(!SnapshotNrDescriptorUse().call,"empty use ring has no latest native call");
    g_nr_descriptor_next_call=2;g_nr_descriptor_uses[1].call=1;g_nr_descriptor_uses[2].call=2;
    Check(SnapshotNrDescriptorUse().call==2,"default snapshot retains latest-call behavior");
    Check(SnapshotNrDescriptorUse(1).call==1,"explicit snapshot selects requested prior call");
    Check(!SnapshotNrDescriptorUse(3).call,"unpublished call does not borrow latest evidence");
    g_nr_descriptor_uses[1].call=65;g_nr_descriptor_next_call=65;
    Check(!SnapshotNrDescriptorUse(1).call,"wrapped slot cannot answer a superseded call");
    Check(SnapshotNrDescriptorUse(65).call==65,"wrapped current call remains selectable");
    g_nr_descriptor_next_call=129;
    Check(!SnapshotNrDescriptorUse().call,"latest accessor also checks exact slot identity");
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
    CachedUse();CopiesAndOverwrites();LifetimesAndCapacity();CompleteKeysAndPools();ConcurrentWrites();CallSnapshots();ClearExtents();
    std::printf("PASS descriptor provenance CPU: %u checks; production registry, copy/reuse/retirement/concurrency/range negatives; no GPU\n",checks.load());
}
