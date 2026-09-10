// Complete production Core + synthetic CPU readback/fence objects. No device,
// GPU work, NGX or game is created. Shader validation calls D3DCompile only.
#include <cstdlib>
#include "store_interval_shader.h"
namespace store_interval_cpu_test {
unsigned checks=0,maps=0,unmaps=0;
std::uint64_t frame=10000,completed=0;
std::uint32_t values[kDiagnosticStatsUintCount]{};
void Check(bool condition,const char *name) {
    ++checks;if(condition)return;std::printf("FAIL %s\n",name);std::exit(41);
}
HRESULT STDMETHODCALLTYPE Map(ID3D12Resource *,UINT,const D3D12_RANGE *range,void **data) {
    Check(range && range->Begin==0 && range->End==192,"STORE_INTERVAL_HEADER_ONLY_MAP");
    ++maps;*data=values;return S_OK;
}
void STDMETHODCALLTYPE Unmap(ID3D12Resource *,UINT,const D3D12_RANGE *range) {
    Check(range && range->Begin==0 && range->End==0,"STORE_INTERVAL_READ_ONLY_UNMAP");++unmaps;
}
UINT64 STDMETHODCALLTYPE Completed(ID3D12Fence *) {return completed;}
nr_store_comparison::Counters Good(unsigned n) {return {nr_store_comparison::kProtocol,n,n,0,0,0,0,0};}
void Seed(NrSlot &slot,ID3D12Resource *readback,const nr_store_comparison::Counters &counts,
        unsigned planned=2040,unsigned capacity=2040,unsigned actual=2040,DXGI_FORMAT format=DXGI_FORMAT_R10G10B10A2_UNORM) {
    std::fill(std::begin(values),std::end(values),0u);values[28]=actual;values[39]=1065;
    values[34]=0x12345678;values[35]=0x87654321;
    std::memcpy(values+40,&counts,sizeof(counts));
    slot.delta_stats_pending=true;slot.delta_stats_readback=readback;
    slot.stored_result_metadata={};auto &metadata=slot.stored_result_metadata;
    metadata.recorded=true;metadata.mode=1;metadata.result_uav=static_cast<unsigned>(format);
    metadata.identity.frame_id=++frame;metadata.identity.config_generation=9;metadata.identity.instance_id=42;
    metadata.feature_generation=77;metadata.probe_policy=NrProbePolicySnapshot();
    metadata.interval_samples=planned;metadata.interval_capacity=capacity;
}
void Run() {
    using namespace nr_store_comparison;
    InitializeCriticalSection(&g_chain_cs);g_lifecycle_tracking_uncertain.store(false);g_config_generation.store(9);
    Check(kDiagnosticStatsBytes==192 && sizeof(Counters)==32,"STORE_INTERVAL_HEADER_ABI");
    Check(PlannedSamples(1920,1080)==2040 && CapacityForExtent(1920,1080,3)==2040 &&
          BufferWords(2040)*4==16512,"STORE_INTERVAL_1080P_ALLOCATION");
    Check(PlannedSamples(3840,2160)==8160 && BufferWords(8160)*4==65472 &&
          PlannedSamples(7680,4320)==32400 && BufferWords(32400)*4==259392,"STORE_INTERVAL_HIGH_RES_ALLOCATION");
    Check(CapacityForExtent(UINT64_MAX,UINT64_MAX,3)==65535 && PlannedSamples(UINT64_MAX,UINT64_MAX)==0 &&
          BufferWords(65535)*4==524472 && BufferWords(UINT_MAX)==48,"STORE_INTERVAL_OVERFLOW_SAFE_CAP");
    Check(PlannedSamples(0,32)==0 && PlannedSamples(32,0)==0 && PlannedSamples(31,31)==1 &&
          PlannedSamples(32,32)==1 && PlannedSamples(33,33)==4,"STORE_INTERVAL_GRID_EDGES");
    Check(CapacityForExtent(1920,1080,1)==0 && CapacityForExtent(1920,1080,4)==0,
          "STORE_INTERVAL_FLOAT_NO_TAIL");
    ID3D12ResourceVtbl resource_table{};resource_table.Map=Map;resource_table.Unmap=Unmap;
    ID3D12Resource resource{&resource_table};auto context=std::make_unique<NrContext>();auto &slot=context->slots[0];
    const auto collect=[&] {CollectSlotDeltaStatsLocked(*context,slot);return SnapshotPostStoreEvidence();};
    Seed(slot,&resource,Good(2040));auto evidence=collect();
    Check(evidence.comparison_method==1 && evidence.comparison_known && evidence.matches_expected &&
          evidence.expected_samples==1065 && evidence.samples==2040 && evidence.expected_hash!=evidence.actual_hash,
          "STORE_INTERVAL_FULL_COVERAGE_IGNORES_UNPAIRED_LEGACY_HASH");
    Check(evidence.interval.written==2040 && evidence.interval.compared==2040 && evidence.interval.flags==0 &&
          evidence.interval_samples==2040 && evidence.interval_capacity==2040,"STORE_INTERVAL_FROZEN_COUNTS");
    auto counts=Good(2040);counts.mismatched=1;Seed(slot,&resource,counts);evidence=collect();
    Check(evidence.comparison_known && !evidence.matches_expected,"STORE_INTERVAL_ONE_MISMATCH");
    counts=Good(2040);--counts.written;--counts.compared;++counts.missing;
    Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && !evidence.matches_expected && (evidence.interval.flags&CoverageIncomplete),
          "STORE_INTERVAL_MISSING_EXPECTATION");
    counts=Good(2040);counts.written*=2;Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&InvalidCounters),"STORE_INTERVAL_STALE_UNCLEARED_HEADER");
    counts=Good(2040);counts.protocol=0;Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&HeaderMismatch),"STORE_INTERVAL_BAD_PROTOCOL");
    counts=Good(2040);counts.flags=MalformedRange;Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&MalformedRange),"STORE_INTERVAL_MALFORMED_RANGE");
    counts=Good(2040);counts.invalid_expected=1;--counts.compared;
    Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && evidence.interval.invalid_expected==1 &&
          (evidence.interval.flags&InvalidInputs),"STORE_INTERVAL_NONFINITE_EXPECTED");
    counts=Good(2040);counts.invalid_actual=1;--counts.compared;Seed(slot,&resource,counts);values[30]=65537;
    evidence=collect();Check(!evidence.comparison_known && evidence.actual_nonfinite && evidence.interval.invalid_actual==1,
          "STORE_INTERVAL_NONFINITE_ACTUAL");
    Seed(slot,&resource,Good(2040));values[30]=131073;evidence=collect();
    Check(!evidence.comparison_known && !evidence.actual_nonfinite && evidence.reference_nonfinite,
          "STORE_INTERVAL_NONFINITE_REFERENCE");
    Seed(slot,&resource,Good(2040),2040,1);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&Capacity),"STORE_INTERVAL_SMALL_ALLOCATION");
    Seed(slot,&resource,Good(2040),2040,0);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&Capacity),"STORE_INTERVAL_HEADER_ONLY_FALLBACK");
    Seed(slot,&resource,Good(65536),65536,65535,65536);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&Capacity),"STORE_INTERVAL_SAMPLE_CAP");
    Seed(slot,&resource,Good(2040),2040,2040,2039);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&CoverageIncomplete),"STORE_INTERVAL_ACTUAL_COVERAGE");
    counts=Good(2040);counts.mismatched=2041;Seed(slot,&resource,counts);evidence=collect();
    Check(!evidence.comparison_known && (evidence.interval.flags&InvalidCounters),"STORE_INTERVAL_IMPOSSIBLE_MISMATCH_COUNT");
    const auto old_phase=NrProbePolicySnapshot();Seed(slot,&resource,Good(2040));
    const auto recorded=slot.stored_result_metadata;slot.post_boundary_trace_id=123;
    SetNrProbePolicy(nr_probe_policy::Mode::Paused);evidence=collect();
    Check(evidence.comparison_known && evidence.matches_expected && evidence.probe_policy.phase==old_phase.phase &&
          evidence.probe_policy.mode==old_phase.mode && evidence.frame==recorded.identity.frame_id &&
          evidence.boundary_trace_id==123,"STORE_INTERVAL_LATE_READBACK_PHASE_FROZEN");
    Seed(slot,&resource,Good(2040));const auto held_frame=slot.stored_result_metadata.identity.frame_id;
    ID3D12FenceVtbl fence_table{};fence_table.GetCompletedValue=Completed;ID3D12Fence fence{&fence_table};
    LifecycleQueue queue{};queue.fence=&fence;slot.last_submission_queue=&queue;slot.completion_fence_value=1;
    const auto before_maps=maps;collect();
    Check(slot.delta_stats_pending && maps==before_maps && SnapshotPostStoreEvidence().frame!=held_frame,
          "STORE_INTERVAL_PENDING_FENCE_NOT_READ");
    completed=1;evidence=collect();Check(!slot.delta_stats_pending && evidence.frame==held_frame && maps==before_maps+1,
          "STORE_INTERVAL_EXACT_FENCE_READBACK");
    slot.completion_fence_value=0;slot.last_submission_queue=nullptr;
    for(const auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R11G11B10_FLOAT}) {
        Seed(slot,&resource,{},0,0,2040,format);values[39]=2040;values[34]=values[35];evidence=collect();
        Check(evidence.comparison_method==0 && evidence.comparison_known && evidence.matches_expected,
              "STORE_INTERVAL_FLOAT_LEGACY_MATCH");
        Seed(slot,&resource,{},0,0,2040,format);values[39]=2040;evidence=collect();
        Check(evidence.comparison_known && !evidence.matches_expected,"STORE_INTERVAL_FLOAT_LEGACY_MISMATCH");
    }
    Seed(slot,&resource,Good(2040),2040,2040,2040,DXGI_FORMAT_R8G8B8A8_UNORM);evidence=collect();
    Check(evidence.comparison_known && evidence.matches_expected,"STORE_INTERVAL_UNORM8_COLLECTOR");
    auto feedback=std::make_unique<FeedbackSession>();FeedbackAppendActivity(*feedback,"end");
    Check(std::strstr(feedback->report,"end.store.post.comparison=method:unorm-legal-code-interval,N:2040,cap:2040,schema:4E525531,written:2040,compared:2040,mismatched:0,missing:0,invalidExpected:0,invalidActual:0,flags:0x0") &&
          std::strstr(feedback->report,"known:1,match:1") && std::strstr(feedback->report,"legacy expected/hash/mean fields retain unique-only"),
          "STORE_INTERVAL_F8_METHOD_AND_COUNTS");
    Check(maps==unmaps,"STORE_INTERVAL_MAP_UNMAP_ONCE");
    for(const auto entry:{"CombineCS","CombineV2CS","ProbePostStoreCS"}) {
        ID3DBlob *blob=nullptr,*error=nullptr;
        const auto hr=D3DCompile(kCodecShader,std::strlen(kCodecShader),"production-store-interval",nullptr,nullptr,
            entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
        if(FAILED(hr) && error)std::printf("%s\n",static_cast<const char *>(error->lpVtbl->GetBufferPointer(error)));
        Check(SUCCEEDED(hr) && blob,"STORE_INTERVAL_PRODUCTION_SHADER_COMPILE");
        ReleaseCom(blob);ReleaseCom(error);
    }
    for(unsigned mode=0;mode<10;++mode) {
        const auto source=store_interval_fixture::Shader(mode);ID3DBlob *blob=nullptr,*error=nullptr;
        const auto hr=D3DCompile(source.data(),source.size(),"store-interval-warp-fixture",nullptr,nullptr,
            "StoreIntervalFixtureCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
        if(FAILED(hr) && error)std::printf("%s\n",static_cast<const char *>(error->lpVtbl->GetBufferPointer(error)));
        Check(SUCCEEDED(hr) && blob,"STORE_INTERVAL_WARP_FIXTURE_SHADER_COMPILE_ONLY");
        ReleaseCom(blob);ReleaseCom(error);
    }
    std::printf("STORE_INTERVAL_CPU checks=%u maps=%u; actual production CollectSlotDeltaStatsLocked and D3DCompile; no GPU/NGX/game\n",checks,maps);
    DeleteCriticalSection(&g_chain_cs);
}
}
int main() {std::setvbuf(stdout,nullptr,_IONBF,0);store_interval_cpu_test::Run();return 0;}
