#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
struct SRWLOCK { std::shared_mutex mutex; };
#define SRWLOCK_INIT {}
inline void AcquireSRWLockShared(SRWLOCK *l){l->mutex.lock_shared();}
inline void ReleaseSRWLockShared(SRWLOCK *l){l->mutex.unlock_shared();}
inline void AcquireSRWLockExclusive(SRWLOCK *l){l->mutex.lock();}
inline void ReleaseSRWLockExclusive(SRWLOCK *l){l->mutex.unlock();}
#define SUCCEEDED(v) ((v)>=0)
#endif
struct FakeObject {std::atomic<unsigned> references{1};};
// No actual COM/D3D objects are supplied by the host tests.
#define IUnknown FakeObject
struct ID3D12Device:FakeObject {std::uint64_t id=11;};
struct ID3D12DescriptorHeap:FakeObject {ID3D12Device* owner=nullptr;};
inline unsigned IUnknown_AddRef(FakeObject* p){return ++p->references;}
inline unsigned IUnknown_Release(FakeObject* p){auto n=--p->references;assert(n);return n;}
inline void ID3D12Device_Release(ID3D12Device* p){IUnknown_Release(p);}
constexpr unsigned IID_ID3D12Device=0;
inline int ID3D12DescriptorHeap_GetDevice(ID3D12DescriptorHeap* p,unsigned,void** o){
    *o=p->owner;if(!p->owner)return -1;IUnknown_AddRef(p->owner);return 0;
}
inline std::uint64_t NrFormatDeviceIdentity(ID3D12Device* p){return p?p->id:0;}
constexpr unsigned D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP=1;
struct ID3D12GraphicsCommandList {unsigned identity;};
struct NativeCommandState {std::uint64_t value=0,check=0,reset_serial=0;bool known=true;unsigned unknown_domains=0;ID3D12DescriptorHeap* heaps[2]{};};
struct CommandStateRecord {std::uint64_t token=0;ID3D12GraphicsCommandList* native=nullptr;NativeCommandState* state=nullptr;
    bool hooks_ready=true;std::uint64_t native_resets=1;bool lifetime_ready=true;};
CommandStateRecord g_command_states[2048]{};
SRWLOCK g_command_state_lock=SRWLOCK_INIT;
std::atomic<unsigned long long> g_command_state_epoch{1};
struct Exclusive {
    Exclusive(){AcquireSRWLockExclusive(&g_command_state_lock);}
    ~Exclusive(){ReleaseSRWLockExclusive(&g_command_state_lock);}
};
#include "production_mutation.inl"
#if !defined(BASELINE_MUTATION)
struct PostIdentity {std::uint64_t token=0,reset=0,address=0;bool known=false;};
PostIdentity SnapshotPostBoundaryList(ID3D12GraphicsCommandList* p){
    CommandStateRegistryReadScope scope;
    for(const auto& e:g_command_states)if(p&&e.native==p&&e.token&&e.lifetime_ready)
        return {e.token,e.native_resets,reinterpret_cast<std::uint64_t>(p),true};
    return {};
}
struct TrackedHeap {std::uint64_t id=0,device=0;bool shader_visible=true;explicit operator bool()const{return id!=0;}};
struct DescriptorRegistry {
    ID3D12DescriptorHeap* a=nullptr;ID3D12DescriptorHeap* b=nullptr;
    TrackedHeap HeapAt(std::uint64_t address)const{
        if(a&&address==reinterpret_cast<std::uint64_t>(a))return {101,11,true};
        if(b&&address==reinterpret_cast<std::uint64_t>(b))return {102,11,true};
        return {};
    }
}g_nr_descriptors;
#include "production_readers.inl"

#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
static unsigned cases=0;
static ID3D12GraphicsCommandList lists[32]{};
static NativeCommandState states[32]{};
static void initialize() {
    Exclusive lock;
    for(auto& e:g_command_states)e={};
    for(unsigned i=0;i<32;++i){states[i]={};lists[i]={i+1};g_command_states[i]={i+1,&lists[i],&states[i],true};}
    g_command_state_epoch.fetch_add(1,std::memory_order_release);
}
static void bump(ID3D12GraphicsCommandList *list) {
    MutateNativeCommandState(list,[](NativeCommandState& s){++s.value;s.check=s.value^0x55AAu;});
}
static bool concurrent_lists() {
    initialize();std::mutex m;std::condition_variable cv;
    unsigned arrived=0,inside=0,peak=0;bool start=false;
    auto task=[&](unsigned index){
        {std::unique_lock lock(m);cv.wait(lock,[&]{return start;});}
        MutateNativeCommandState(&lists[index],[&](NativeCommandState& s){
            std::unique_lock lock(m);++inside;++arrived;peak=std::max(peak,inside);cv.notify_all();
            cv.wait_for(lock,500ms,[&]{return arrived==2;});
            ++s.value;--inside;
        });
    };
    std::thread a(task,0),b(task,1);
    {std::lock_guard lock(m);start=true;}cv.notify_all();a.join();b.join();
    assert(states[0].value==1&&states[1].value==1);
    std::cout<<"independent-list simultaneous mutations="<<peak<<"\n";
    return peak==2;
}
int mutation_tests(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="--contention-only")return concurrent_lists()?0:3;
    assert(concurrent_lists());++cases;
    initialize();
    // No watched state is invented for null, unregistered or not-ready lists.
    bump(nullptr);ID3D12GraphicsCommandList unknown{900};bump(&unknown);assert(states[0].value==0);++cases;
    {Exclusive lock;g_command_states[0].hooks_ready=false;}bump(&lists[0]);assert(states[0].value==0);++cases;
    {Exclusive lock;g_command_states[0].hooks_ready=true;}bump(&lists[0]);assert(states[0].value==1);++cases;
    // Cached negatives must recover once the true registration epoch changes.
    {Exclusive lock;g_command_states[0].state=nullptr;g_command_state_epoch.fetch_add(1);}
    bump(&lists[0]);{Exclusive lock;g_command_states[0].state=&states[0];g_command_state_epoch.fetch_add(1);}
    bump(&lists[0]);assert(states[0].value==2);++cases;
    // Same native address, a new record/token/state lifetime. The old cache is not proof.
    {Exclusive lock;g_command_states[0]={101,&lists[0],&states[10],true};g_command_state_epoch.fetch_add(1);}
    bump(&lists[0]);assert(states[0].value==2&&states[10].value==1);++cases;
    // Previously unseen command list becomes registered later: delayed DLSS.
    ID3D12GraphicsCommandList late{901};bump(&late);
    {Exclusive lock;g_command_states[32]={202,&late,&states[12],true};g_command_state_epoch.fetch_add(1);}
    bump(&late);assert(states[12].value==1);++cases;
    // The same address is republished in a DIFFERENT registry slot.
    {Exclusive lock;g_command_states[0]={};g_command_states[33]={203,&lists[0],&states[11],true};g_command_state_epoch.fetch_add(1);}
    bump(&lists[0]);assert(states[11].value==1&&states[10].value==1);++cases;
    // The same list still serializes every mutation: no TryAcquire/drop path.
    initialize();{std::vector<std::thread> workers;
        for(unsigned t=0;t<8;++t)workers.emplace_back([]{for(unsigned i=0;i<20000;++i)bump(&lists[0]);});
        for(auto& t:workers)t.join();assert(states[0].value==160000&&states[0].check==(160000^0x55AAu));}++cases;
    // Concurrent lists retain independent state exactly.
    initialize();{std::vector<std::thread> workers;
        for(unsigned t=0;t<8;++t)workers.emplace_back([t]{for(unsigned i=0;i<20000;++i)bump(&lists[t]);});
        for(auto& t:workers)t.join();for(unsigned t=0;t<8;++t)assert(states[t].value==20000);}++cases;
    // Capture/reset/retirement still use global exclusive ownership, and must
    // wait while any live mutation is executing even on a different list.
    for(unsigned which=0;which<3;++which){initialize();std::promise<void> entered,release,writer_started;auto wait=release.get_future().share();
        auto worker=std::async(std::launch::async,[&]{MutateNativeCommandState(&lists[0],[&](NativeCommandState& s){entered.set_value();wait.wait();s.value=7;s.check=7^0x55AAu;});});
        entered.get_future().wait();
        auto writer=std::async(std::launch::async,[&]{writer_started.set_value();Exclusive lock;
            assert(states[0].value==7&&states[0].check==(7^0x55AAu));
            if(which==0){assert(g_command_states[0].state==&states[0]);}
            if(which==1){states[0]={};states[0].reset_serial=1;}
            if(which==2){g_command_states[0]={};g_command_state_epoch.fetch_add(1);}});
        writer_started.get_future().wait();assert(writer.wait_for(30ms)==std::future_status::timeout);
        release.set_value();worker.get();writer.get();++cases;
    }
#if !defined(BASELINE_MUTATION)
    // Shared-registry readers of state take the same slot lock (descriptor heap readers).
    initialize();{std::promise<void> entered,release,reader_started;auto wait=release.get_future().share();
        auto worker=std::async(std::launch::async,[&]{MutateNativeCommandState(&lists[0],[&](NativeCommandState& s){s.value=9;entered.set_value();wait.wait();s.check=9^0x55AAu;});});
        entered.get_future().wait();
        auto reader=std::async(std::launch::async,[&]{CommandStateRegistryReadScope registry;reader_started.set_value();
            CommandStateSlotScope slot(g_command_states[0]);assert(states[0].check==(states[0].value^0x55AAu));});
        reader_started.get_future().wait();assert(reader.wait_for(30ms)==std::future_status::timeout);
        release.set_value();worker.get();reader.get();}++cases;
    // Local locks unwind on exceptions; don't leave a process-wide deadlock.
    initialize();try{MutateNativeCommandState(&lists[0],[](NativeCommandState&){throw std::runtime_error("fixture");});}catch(const std::runtime_error&){}
    bump(&lists[0]);{Exclusive lock;assert(states[0].value==1);}++cases;
#endif
    // A writer changes lifetimes while independent setters are active.
    initialize();{std::atomic<bool> stop=false;std::vector<std::thread> workers;
        for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{while(!stop.load(std::memory_order_acquire))bump(&lists[t]);});
        for(unsigned round=0;round<2000;++round){Exclusive lock;unsigned t=round%4;auto& r=g_command_states[t];
            assert(!states[t].value||states[t].check==(states[t].value^0x55AAu));
            r.hooks_ready=(round%3)!=0;r.token+=32;g_command_state_epoch.fetch_add(1,std::memory_order_release);}
        stop.store(true,std::memory_order_release);for(auto& t:workers)t.join();}++cases;
    std::cout<<"production-mutation scenarios passed="<<cases<<"; native D3D12/game not exercised\n";
    return 0;
}

#if !defined(BASELINE_MUTATION)
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
void reader_tests(){unsigned cases=0;ID3D12Device device;ID3D12DescriptorHeap a,b;a.owner=b.owner=&device;
    ID3D12GraphicsCommandList list{1};NativeCommandState state;state.heaps[0]=&a;
    g_command_states[0]={1,&list,&state,true,1,true};g_nr_descriptors={&a,&b};++g_command_state_epoch;
    assert(NrDescriptorBoundHeap(&list)==101);++cases;
    assert(NrDescriptorBoundGpuHeapMatches(&list,101,11));++cases;
    assert(!NrDescriptorBoundGpuHeapMatches(&list,102,11)&&!NrDescriptorBoundGpuHeapMatches(&list,101,12));++cases;
    assert(device.references==1&&a.references==1);++cases;
    state.known=false;assert(NrDescriptorBoundHeap(&list)==0&&!NrDescriptorBoundGpuHeapMatches(&list,101,11));state.known=true;++cases;
    state.unknown_domains=1;assert(NrDescriptorBoundHeap(&list)==0&&!NrDescriptorBoundGpuHeapMatches(&list,101,11));state.unknown_domains=0;++cases;
    assert(!NrDescriptorBoundHeap(nullptr)&&!NrDescriptorBoundGpuHeapMatches(nullptr,101,11));++cases;
    for(unsigned which=0;which<2;++which){state.heaps[0]=&a;std::promise<void> entered,release,reader_started;auto gate=release.get_future().share();
        auto mutator=std::async(std::launch::async,[&]{MutateNativeCommandState(&list,[&](NativeCommandState& s){s.heaps[0]=nullptr;entered.set_value();gate.wait();s.heaps[0]=&b;});});
        entered.get_future().wait();
        auto reader=std::async(std::launch::async,[&]{reader_started.set_value();return which==0?NrDescriptorBoundHeap(&list)==102:NrDescriptorBoundGpuHeapMatches(&list,102,11);});
        reader_started.get_future().wait();assert(reader.wait_for(30ms)==std::future_status::timeout);
        release.set_value();mutator.get();assert(reader.get());++cases;
    }
    assert(device.references==1&&a.references==1&&b.references==1);++cases;
    std::atomic<bool> done=false;
    std::thread writer([&]{for(unsigned i=0;i<20000;++i)MutateNativeCommandState(&list,[&](NativeCommandState& s){s.heaps[0]=(i%2)?&a:&b;});done=true;});
    do{const auto id=NrDescriptorBoundHeap(&list);assert(id==101||id==102);(void)NrDescriptorBoundGpuHeapMatches(&list,101,11);}while(!done.load());
    writer.join();assert(device.references==1&&a.references==1&&b.references==1);++cases;
    std::cout<<"actual descriptor-reader scenarios passed="<<cases<<"; D3D12/COM objects are test doubles\n";
}

#endif
int main(int argc,char** argv) {
    const int result=mutation_tests(argc,argv);
#if !defined(BASELINE_MUTATION)
    if(argc==1)reader_tests();
#endif
    return result;
}
