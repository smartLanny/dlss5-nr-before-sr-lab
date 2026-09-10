#include "src/nr_descriptor_provenance.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace nr_descriptor;
void Require(bool value,const char *message) {
    if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(41);}
}
int main() {
    auto registry=std::make_unique<Registry<>>();
    const Resource output{3448,7,0x5000};registry->ObserveResource(output);
    const Heap cpu{10,7,0x100,0x100000,0,32,32,0,false};
    Require(registry->ObserveHeap(cpu),"persistent CPU heap");
    const View value{output,Kind::Uav,28,0,0,1920,1080,true,true,true};
    auto write=registry->BeginWrite(cpu.cpu,0,value);registry->Finish(write);
    const auto source=registry->Cpu(cpu.cpu,0);Require(static_cast<bool>(source),"persistent CPU source");
    std::array<Heap,8> heaps;
    for(unsigned i=0;i<heaps.size();++i) {
        heaps[i]={20+i,7,0x1000+i*64,0x1000000ull+i*0x100000ull,0x2000000ull+i*0x100000ull,16384,32,0,true};
        Require(registry->ObserveHeap(heaps[i]),"distinct GPU heap range");
    }
    std::printf("registry_bytes=%zu heap_capacity=4096 resource_capacity=32768 cpu_slot_capacity=32768 gpu_slot_capacity=65536\n",sizeof(Registry<>));
    unsigned next=0;
    for(const unsigned copies:{500000u,1000000u}) {
        const auto before=registry->SnapshotCounters();
        const auto started=std::chrono::steady_clock::now();
        for(unsigned i=0;i<copies;++i,++next) {
            const auto &heap=heaps[next%heaps.size()];
            const unsigned slot=(next/heaps.size())%heap.count;
            const auto copy=registry->BeginCopy(heap.cpu+static_cast<Id>(slot)*heap.stride,cpu.cpu,0);
            registry->Finish(copy);
        }
        const auto stopped=std::chrono::steady_clock::now();
        const auto after=registry->SnapshotCounters();
        const bool retained=registry->Current(source);
        const auto &last_heap=heaps[(next-1)%heaps.size()];
        const unsigned last_slot=((next-1)/heaps.size())%last_heap.count;
        const auto last=registry->UavPair(7,output,last_heap.id,last_heap.gpu+static_cast<Id>(last_slot)*last_heap.stride,cpu.cpu);
        Require(retained&&static_cast<bool>(last),"CPU source and newest copied GPU descriptor remain paired");
        Require(after.copies-before.copies==copies&&after.writes-before.writes==copies,"all requested descriptor elements observed");
        Require(after.cpu_slots.active==1&&!after.cpu_slots.evictions&&!after.cpu_slots.drops&&
            after.gpu_slots.active==65536&&!after.gpu_slots.drops&&!after.unknown_writes&&!after.heaps.coverage_lost&&
            !after.resources.coverage_lost,"turnover confined to GPU slot pool");
        const auto elapsed=std::chrono::duration<double,std::milli>(stopped-started).count();
        std::printf("{\"copies\":%u,\"elapsed_ms\":%.3f,\"copies_per_second\":%.0f,\"cpu_source_retained\":true,\"cpu_active\":%u,\"gpu_active\":%u,\"cpu_evictions_delta\":%llu,\"gpu_evictions_delta\":%llu,\"drops_delta\":%llu,\"unknown_writes_delta\":%llu}\n",
            copies,elapsed,copies*1000.0/elapsed,after.cpu_slots.active,after.gpu_slots.active,
            after.cpu_slots.evictions-before.cpu_slots.evictions,after.gpu_slots.evictions-before.gpu_slots.evictions,
            (after.cpu_slots.drops-before.cpu_slots.drops)+(after.gpu_slots.drops-before.gpu_slots.drops),
            after.unknown_writes-before.unknown_writes);
    }
    std::printf("PASS bounded CPU copy throughput and retention observation; no timing threshold, D3D, GPU, NVIDIA or game claim\n");
}
