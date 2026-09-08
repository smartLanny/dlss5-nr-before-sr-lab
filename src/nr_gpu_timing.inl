// Included after D3D12 C declarations and ReleaseCom. All operations are bounded;
// there is no GPU wait, and inability to sample never changes NR admission.
#include "nr_gpu_timing.h"
class NrGpuTimingRuntime {
    using Ticket=nr_gpu_timing::Ticket;
    using Boundary=nr_gpu_timing::Boundary;
    using Lifetime=nr_gpu_timing::Lifetime;
    using Key=nr_gpu_timing::Key;
    using Snapshot=nr_gpu_timing::Snapshot;
    static constexpr Ticket kNoTicket=nr_gpu_timing::kNoTicket;
    static constexpr unsigned kCapacity=96, kQueries=static_cast<unsigned>(Boundary::Count);
    struct Sample {
        Lifetime lifetime{};
        Ticket ticket=0;
        std::uint64_t epoch=0, frequency=0;
        ID3D12QueryHeap *heap=nullptr;
        ID3D12Resource *readback=nullptr;
        ID3D12Fence *fence=nullptr;
        ID3D12Device *device=nullptr;
        unsigned mask=0;
        bool finished=false, valid=false;
    };
    SRWLOCK lock_=SRWLOCK_INIT;
    Sample samples_[kCapacity]{};
    Snapshot stats_{};
    Ticket next_=1;
    std::uint64_t epoch_=0;
    Sample *Find(Ticket ticket) {
        if(!ticket)return nullptr;
        for(auto &s:samples_)if(s.ticket==ticket)return &s;
        return nullptr;
    }
    static void Stamp(Sample &s, ID3D12GraphicsCommandList *list, Boundary boundary) {
        const unsigned index=static_cast<unsigned>(boundary), bit=1u<<index;
        if((s.mask&bit)==0){ID3D12GraphicsCommandList_EndQuery(list,s.heap,D3D12_QUERY_TYPE_TIMESTAMP,index);s.mask|=bit;}
    }
    void PollLocked() {
        for(auto &s:samples_){
            if(!s.ticket)continue;
            const auto complete=s.fence?ID3D12Fence_GetCompletedValue(s.fence):0;
            if(!s.lifetime.Retirable(complete))continue;
            if(s.finished && s.valid && !s.lifetime.replayed && s.frequency && s.epoch==epoch_){
                void *mapped=nullptr;
                const D3D12_RANGE range={0,kQueries*sizeof(UINT64)};
                bool ok=SUCCEEDED(ID3D12Resource_Map(s.readback,0,&range,&mapped))&&mapped;
                UINT64 values[kQueries]{};
                if(ok){memcpy(values,mapped,sizeof(values));const D3D12_RANGE written={0,0};ID3D12Resource_Unmap(s.readback,0,&written);}
                for(unsigned i=1;ok && i<kQueries;++i)ok=values[i]>=values[i-1];
                if(ok){
                    const double scale=1000.0/static_cast<double>(s.frequency);
                    const double n=static_cast<double>(++stats_.completed);
                    stats_.encoding_ms+=(double(values[1]-values[0])*scale-stats_.encoding_ms)/n;
                    stats_.feature18_ms+=(double(values[3]-values[2])*scale-stats_.feature18_ms)/n;
                    stats_.composition_ms+=(double(values[4]-values[3])*scale-stats_.composition_ms)/n;
                    stats_.sr_ms+=(double(values[6]-values[5])*scale-stats_.sr_ms)/n;
                    stats_.available=true;
                }else ++stats_.invalid;
            }else ++stats_.invalid;
            ReleaseCom(s.fence);
            Sample reusable{};reusable.heap=s.heap;reusable.readback=s.readback;reusable.device=s.device;s=reusable;
        }
    }
public:
    Ticket Begin(ID3D12Device *device,ID3D12GraphicsCommandList *list,std::uint64_t token,const Key &key) {
        if(!device||!list||!token)return kNoTicket;
        AcquireSRWLockExclusive(&lock_);
        PollLocked();
        if(!epoch_ || !stats_.key.Same(key)){const auto dropped=stats_.dropped,invalid=stats_.invalid;stats_={};stats_.key=key;stats_.dropped=dropped;stats_.invalid=invalid;++epoch_;}
        Sample *s=nullptr;for(auto &candidate:samples_)if(!candidate.ticket){s=&candidate;break;}
        if(!s){++stats_.dropped;ReleaseSRWLockExclusive(&lock_);return kNoTicket;}
        D3D12_QUERY_HEAP_DESC query={};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=kQueries;
        D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=kQueries*sizeof(UINT64);desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(s->device!=device){ReleaseCom(s->readback);ReleaseCom(s->heap);ReleaseCom(s->device);s->device=device;ID3D12Device_AddRef(device);}
        const HRESULT q=s->heap?S_OK:ID3D12Device_CreateQueryHeap(device,&query,IID_ID3D12QueryHeap,reinterpret_cast<void **>(&s->heap));
        const HRESULT r=s->readback?S_OK:(SUCCEEDED(q)?ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&s->readback)):E_FAIL);
        if(FAILED(q)||FAILED(r)||!s->heap||!s->readback){ReleaseCom(s->readback);ReleaseCom(s->heap);ReleaseCom(s->device);*s={};++stats_.dropped;ReleaseSRWLockExclusive(&lock_);return kNoTicket;}
        s->ticket=next_++;if(!s->ticket)s->ticket=next_++;
        s->epoch=epoch_;s->lifetime.Record(token);
        Stamp(*s,list,Boundary::EncodeBegin);
        const Ticket ticket=s->ticket;
        ReleaseSRWLockExclusive(&lock_);return ticket;
    }
    void Mark(Ticket ticket,ID3D12GraphicsCommandList *list,Boundary boundary) {
        if(!ticket||!list)return;
        AcquireSRWLockExclusive(&lock_);
        if(auto *s=Find(ticket);s&&!s->finished&&!s->lifetime.unknown)Stamp(*s,list,boundary);
        ReleaseSRWLockExclusive(&lock_);
    }
    void BeforeSr(Ticket ticket,ID3D12GraphicsCommandList *list) {
        if(!ticket||!list)return;
        AcquireSRWLockExclusive(&lock_);
        if(auto *s=Find(ticket);s&&!s->finished&&!s->lifetime.unknown){
            // Fill missing boundaries on failed/warmup paths before Resolve;
            // such samples remain invalid and cannot become performance data.
            s->valid=s->mask==((1u<<static_cast<unsigned>(Boundary::SrBegin))-1);
            for(unsigned i=0;i<=static_cast<unsigned>(Boundary::SrBegin);++i)Stamp(*s,list,static_cast<Boundary>(i));
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    void End(Ticket ticket,ID3D12GraphicsCommandList *list,bool successful) {
        if(!ticket||!list)return;
        AcquireSRWLockExclusive(&lock_);
        if(auto *s=Find(ticket);s&&!s->finished&&!s->lifetime.unknown){
            s->valid &= successful;
            for(unsigned i=0;i<kQueries;++i)Stamp(*s,list,static_cast<Boundary>(i));
            ID3D12GraphicsCommandList_ResolveQueryData(list,s->heap,D3D12_QUERY_TYPE_TIMESTAMP,0,kQueries,s->readback,0);s->finished=true;
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    void OnSubmit(std::uint64_t token,ID3D12CommandQueue *queue,ID3D12Fence *fence,std::uint64_t value) {
        if(!token)return;
        AcquireSRWLockExclusive(&lock_);
        for(auto &s:samples_)if(s.ticket&&s.lifetime.token==token){
            if(!s.lifetime.Submit(reinterpret_cast<std::uintptr_t>(queue),reinterpret_cast<std::uintptr_t>(fence),value)){s.valid=false;continue;}
            if(!s.fence){s.fence=fence;ID3D12Fence_AddRef(fence);}
            if(FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue,&s.frequency)))s.valid=false;
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    void OnReset(std::uint64_t token,bool definitely_unsubmitted) {
        AcquireSRWLockExclusive(&lock_);
        for(auto &s:samples_)if(s.ticket&&s.lifetime.token==token)s.lifetime.Reset(definitely_unsubmitted);
        PollLocked();ReleaseSRWLockExclusive(&lock_);
    }
    void Quarantine(std::uint64_t token=0) {
        AcquireSRWLockExclusive(&lock_);
        for(auto &s:samples_)if(s.ticket&&(!token||s.lifetime.token==token)){s.lifetime.unknown=true;s.valid=false;}
        ReleaseSRWLockExclusive(&lock_);
    }
    Snapshot Read() {
        AcquireSRWLockExclusive(&lock_);PollLocked();
        Snapshot result=stats_;for(const auto &s:samples_)result.pending+=s.ticket!=0;
        ReleaseSRWLockExclusive(&lock_);return result;
    }
    // No destructor frees potentially recorded objects during DLL detach.
    // Safely retired samples are reclaimed by Read/OnReset; unknown work stays pinned.
};
NrGpuTimingRuntime g_gpu_timing;
