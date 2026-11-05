namespace nr_dual_runtime {
constexpr unsigned kTimeStages=7,kTimeQueries=14,kTimeWindow=120;
std::atomic<unsigned> time_samples{0},time_config{0};
std::atomic<unsigned long long> time_warmup_samples{0};
std::atomic<double> time_median[kTimeStages]{},time_p95[kTimeStages]{},time_last[kTimeStages]{};
double time_values[kTimeStages][kTimeWindow]{};
unsigned time_count=0,time_cursor=0,time_generation=0;
struct FullLayerTiming {
    double values[kTimeWindow]{};unsigned count=0,cursor=0,generation=0;
    std::atomic<unsigned> samples{0},config{0};
    std::atomic<double> last{0},median{0},p95{0};
};
FullLayerTiming full_time[nr_full::kMaxPasses]{};
void RecordFullTiming(unsigned layer,unsigned generation,double ms) {
    if(layer>=nr_full::kMaxPasses||!std::isfinite(ms)||ms>10000)return;
    auto& out=full_time[layer];
    if(out.generation!=generation){out.generation=generation;out.count=out.cursor=0;}
    out.values[out.cursor]=ms;out.cursor=(out.cursor+1)%kTimeWindow;
    out.count=std::min(out.count+1,kTimeWindow);
    double sorted[kTimeWindow]{};std::copy_n(out.values,out.count,sorted);std::sort(sorted,sorted+out.count);
    out.last.store(ms);out.median.store(out.count&1?sorted[out.count/2]:(sorted[out.count/2-1]+sorted[out.count/2])*.5);
    out.p95.store(sorted[(out.count*95+99)/100-1]);out.config.store(generation);out.samples.store(out.count);
}
void CollectTiming(NrSlot& slot) {
    if(!slot.dual_time_pending||!CanMutateSlotLocked(slot))return;
    if(!slot.dual_time_fence||!slot.dual_time_queue) {
        // The recording was discarded before a confirmed submission. A stale
        // older slot fence cannot authenticate this query recording.
        slot.dual_time_pending=false;return;
    }
    auto* queue=slot.dual_time_queue;
    const auto complete=queue->fence?ID3D12Fence_GetCompletedValue(queue->fence):UINT64_MAX;
    if(complete==UINT64_MAX||complete<slot.dual_time_fence)return;
    slot.dual_time_pending=false;
    UINT64 frequency=0;
    if(!queue->queue||FAILED(ID3D12CommandQueue_GetTimestampFrequency(queue->queue,&frequency))||!frequency)return;
    const D3D12_RANGE range{0,kTimeQueries*sizeof(UINT64)};void* mapped=nullptr;
    if(FAILED(ID3D12Resource_Map(slot.dual_time_readback,0,&range,&mapped))||!mapped)return;
    UINT64 ticks[kTimeQueries]{};std::memcpy(ticks,mapped,sizeof(ticks));
    const D3D12_RANGE empty{0,0};ID3D12Resource_Unmap(slot.dual_time_readback,0,&empty);
    if(slot.dual_time_generation!=g_config_generation.load())return;
    if(!slot.dual_time_stable){time_warmup_samples.fetch_add(1);return;}
    if(slot.dual_full_timing_layer) {
        const unsigned first=slot.dual_full_timing_layer==1?0u:4u;
        const unsigned pair=3u<<first;
        if((slot.dual_time_mask&pair)!=pair||ticks[first+1]<ticks[first])return;
#if defined(NR_HAS_MEMORY_OBSERVATION)
        if(slot.dual_full_timing_layer==1){
            auto key=slot.memory_timing_key;key.queue=queue->identity;
            g_nr_memory_gpu_window.Observe(key,double(ticks[first+1]-ticks[first])*1000.0/double(frequency));
        }
#endif
        RecordFullTiming(slot.dual_full_timing_layer-1,slot.dual_time_generation,
            double(ticks[first+1]-ticks[first])*1000.0/double(frequency));
        return;
    }
    double ms[kTimeStages]{};
    for(unsigned i=0;i<kTimeStages;++i) {
        const unsigned pair=3u<<(i*2);
        if((slot.dual_time_mask&pair)!=pair) {
            if(!slot.dual_time_reduced&&(i==1||i==6))continue;
            return;
        }
        if(ticks[i*2+1]<ticks[i*2])return;
        ms[i]=double(ticks[i*2+1]-ticks[i*2])*1000.0/double(frequency);
        if(!std::isfinite(ms[i])||ms[i]>10000)return;
    }
    if(time_generation!=slot.dual_time_generation){time_generation=slot.dual_time_generation;time_count=time_cursor=0;}
#if defined(NR_HAS_MEMORY_OBSERVATION)
    auto key=slot.memory_timing_key;key.queue=queue->identity;
    g_nr_memory_gpu_window.Observe(key,ms[0]);
#endif
    for(unsigned i=0;i<kTimeStages;++i)time_values[i][time_cursor]=ms[i];
    time_cursor=(time_cursor+1)%kTimeWindow;time_count=std::min(time_count+1,kTimeWindow);
    for(unsigned i=0;i<kTimeStages;++i) {
        double sorted[kTimeWindow]{};std::copy_n(time_values[i],time_count,sorted);std::sort(sorted,sorted+time_count);
        const double median=time_count&1?sorted[time_count/2]:(sorted[time_count/2-1]+sorted[time_count/2])*.5;
        time_last[i].store(ms[i]);time_median[i].store(median);
        time_p95[i].store(sorted[(time_count*95+99)/100-1]);
    }
    time_config.store(time_generation,std::memory_order_release);
    time_samples.store(time_count,std::memory_order_release);second_completed.fetch_add(1);
}
bool PrepareTiming(ID3D12Device* device,NrSlot& slot,unsigned full_layer=0) {
    if(!CanMutateSlotLocked(slot))return false;
    CollectTiming(slot);
    slot.dual_full_timing_layer=full_layer;
    slot.dual_time_active=false;slot.dual_time_mask=0;slot.dual_time_stable=false;
    slot.dual_time_token=slot.dual_time_fence=0;slot.dual_time_queue=nullptr;
    if(!slot.dual_time_heap) {
        D3D12_QUERY_HEAP_DESC d{};d.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;d.Count=kTimeQueries;
        if(FAILED(ID3D12Device_CreateQueryHeap(device,&d,IID_ID3D12QueryHeap,reinterpret_cast<void**>(&slot.dual_time_heap))))return false;
    }
    if(!slot.dual_time_readback) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=kTimeQueries*sizeof(UINT64);
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_READBACK;h.CreationNodeMask=h.VisibleNodeMask=1;
        if(FAILED(ID3D12Device_CreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&slot.dual_time_readback))))return false;
    }
    if(StageCaptureStatus()!=0||nr_f8::IsActive()||nr_replay::Active())return false;
    slot.dual_time_generation=g_config_generation.load();slot.dual_time_active=true;return true;
}
void Stamp(ID3D12GraphicsCommandList* list,NrSlot& slot,unsigned query) {
    if(!slot.dual_time_active||query>=kTimeQueries||!slot.dual_time_heap||fatal_result.load())return;
    unsigned long long token=0;
    if(!ReadLifecycleToken(list,token)||!token)return;
    if(slot.dual_time_token&&slot.dual_time_token!=token)return;
    if(slot.dual_time_mask&(1u<<query))return;
    slot.dual_time_token=token;
    ID3D12GraphicsCommandList_EndQuery(list,slot.dual_time_heap,D3D12_QUERY_TYPE_TIMESTAMP,query);
    slot.dual_time_mask|=1u<<query;
}
void Clock(void* user,ID3D12GraphicsCommandList* list,unsigned query){Stamp(list,*static_cast<NrSlot*>(user),query);}
void ResolveTiming(ID3D12GraphicsCommandList* list,NrSlot& slot,bool visible) {
    if(!slot.dual_time_active||fatal_result.load())return;
    if(slot.dual_time_mask&(1u<<10))Stamp(list,slot,11);
    slot.dual_time_stable=visible&&slot.dual_selected!=nullptr&&slot.dual_capture_index==nr_replay::Recorder::Invalid&&StageCaptureStatus()==0&&!nr_f8::IsActive()&&!nr_replay::Active();
    slot.dual_time_reduced=slot.dual_reduced;
    // Resolve only runs actually EndQuery'd; skipped direct-path downsample or
    // a warmup's missing secondary invocation must never resolve stale queries.
    for(unsigned i=0;i<kTimeQueries;) {
        if(!(slot.dual_time_mask&(1u<<i))){++i;continue;}
        const unsigned begin=i;while(i<kTimeQueries&&(slot.dual_time_mask&(1u<<i)))++i;
        ID3D12GraphicsCommandList_ResolveQueryData(list,slot.dual_time_heap,D3D12_QUERY_TYPE_TIMESTAMP,
            begin,i-begin,slot.dual_time_readback,SIZE_T(begin)*sizeof(UINT64));
    }
    slot.dual_time_pending=slot.dual_time_mask!=0;slot.dual_time_active=false;
}
void ResolveLayerTiming(ID3D12GraphicsCommandList* list,NrSlot& slot,bool stable) {
    if(!slot.dual_time_active||fatal_result.load())return;
    slot.dual_time_stable=stable&&StageCaptureStatus()==0&&!nr_f8::IsActive()&&!nr_replay::Active();
    const unsigned pair=3u<<4;
    if((slot.dual_time_mask&pair)==pair) {
        ID3D12GraphicsCommandList_ResolveQueryData(list,slot.dual_time_heap,D3D12_QUERY_TYPE_TIMESTAMP,
            4,2,slot.dual_time_readback,4*sizeof(UINT64));
        slot.dual_time_pending=true;
    }
    slot.dual_time_active=false;
}
}
