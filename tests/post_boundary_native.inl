// CPU-only COM fixture calling the real native detours through real MinHook
// trampolines. No D3D device, command recording runtime, GPU or NGX is created.
namespace boundary_native_test {
using queue_test::Check;
volatile unsigned forwarded=0;
unsigned execute_returns=0;
bool expose_extended=true,tear_off=false;
ID3D12GraphicsCommandList7Vtbl table;
ID3D12GraphicsCommandList7 object{&table},other{&table};
const void *last_pointer=nullptr;
UINT last_count=0;
__declspec(noinline) HRESULT STDMETHODCALLTYPE Query(ID3D12GraphicsCommandList7 *self,REFIID iid,void **out) {
    *out=nullptr;
    if(IsEqualGUID(iid,IID_ID3D12GraphicsCommandList7)&&expose_extended){*out=tear_off?&other:self;return S_OK;}
    return E_NOINTERFACE;
}
__declspec(noinline) ULONG STDMETHODCALLTYPE Release(ID3D12GraphicsCommandList7 *) {forwarded=forwarded+101;return 1;}
__declspec(noinline) void STDMETHODCALLTYPE Barriers(ID3D12GraphicsCommandList *list,UINT n,const D3D12_RESOURCE_BARRIER *b) {
    forwarded=forwarded+1;last_pointer=b;last_count=n;(void)list;
}
__declspec(noinline) void STDMETHODCALLTYPE Enhanced(ID3D12GraphicsCommandList7 *,UINT n,const D3D12_BARRIER_GROUP *b) {
    forwarded=forwarded+2;last_pointer=b;last_count=n;
}
__declspec(noinline) void STDMETHODCALLTYPE Copy(ID3D12GraphicsCommandList *,ID3D12Resource *d,ID3D12Resource *) {forwarded=forwarded+3;last_pointer=d;}
__declspec(noinline) void STDMETHODCALLTYPE Region(ID3D12GraphicsCommandList *,const D3D12_TEXTURE_COPY_LOCATION *d,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION *,const D3D12_BOX *) {forwarded=forwarded+4;last_pointer=d;}
__declspec(noinline) void STDMETHODCALLTYPE ClearUint(ID3D12GraphicsCommandList *,D3D12_GPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,ID3D12Resource *r,const UINT *,UINT,const D3D12_RECT *) {forwarded=forwarded+5;last_pointer=r;}
__declspec(noinline) void STDMETHODCALLTYPE ClearFloat(ID3D12GraphicsCommandList *,D3D12_GPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,ID3D12Resource *r,const FLOAT *,UINT,const D3D12_RECT *) {forwarded=forwarded+6;last_pointer=r;}
__declspec(noinline) void STDMETHODCALLTYPE ClearRtv(ID3D12GraphicsCommandList *,D3D12_CPU_DESCRIPTOR_HANDLE,const FLOAT *v,UINT,const D3D12_RECT *) {forwarded=forwarded+7;last_pointer=v;}
__declspec(noinline) void STDMETHODCALLTYPE Draw(ID3D12GraphicsCommandList *,UINT,UINT,UINT,UINT) {forwarded=forwarded+8;}
__declspec(noinline) void STDMETHODCALLTYPE Indexed(ID3D12GraphicsCommandList *,UINT,UINT,UINT,INT,UINT) {forwarded=forwarded+9;}
__declspec(noinline) void STDMETHODCALLTYPE Dispatch(ID3D12GraphicsCommandList *,UINT,UINT,UINT) {forwarded=forwarded+10;}
__declspec(noinline) void STDMETHODCALLTYPE Resolve(ID3D12GraphicsCommandList *,ID3D12Resource *r,UINT,ID3D12Resource *,UINT,DXGI_FORMAT) {forwarded=forwarded+11;last_pointer=r;}
__declspec(noinline) void STDMETHODCALLTYPE Discard(ID3D12GraphicsCommandList *,ID3D12Resource *r,const D3D12_DISCARD_REGION *) {forwarded=forwarded+12;last_pointer=r;}
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue *,UINT n,ID3D12CommandList *const *lists) {++execute_returns;last_count=n;last_pointer=lists;}
void Initialize() {
    table.QueryInterface=Query;table.Release=Release;
#define NATIVE_METHOD(NAME,FN) table.NAME=reinterpret_cast<decltype(table.NAME)>(&FN)
    NATIVE_METHOD(ResourceBarrier,Barriers);NATIVE_METHOD(Barrier,Enhanced);NATIVE_METHOD(CopyResource,Copy);NATIVE_METHOD(CopyTextureRegion,Region);
    NATIVE_METHOD(ClearUnorderedAccessViewUint,ClearUint);NATIVE_METHOD(ClearUnorderedAccessViewFloat,ClearFloat);NATIVE_METHOD(ClearRenderTargetView,ClearRtv);
    NATIVE_METHOD(DrawInstanced,Draw);NATIVE_METHOD(DrawIndexedInstanced,Indexed);NATIVE_METHOD(Dispatch,Dispatch);NATIVE_METHOD(ResolveSubresource,Resolve);NATIVE_METHOD(DiscardResource,Discard);
#undef NATIVE_METHOD
    auto &record=g_command_states[0];record={};record.token=1001;
    record.native=reinterpret_cast<ID3D12GraphicsCommandList *>(&object);record.lifetime_ready=true;record.meta_contract.device_identity=17;
    auto &second=g_command_states[1];second={};second.token=1002;
    second.native=reinterpret_cast<ID3D12GraphicsCommandList *>(&other);second.lifetime_ready=true;second.meta_contract.device_identity=17;
    g_command_state_epoch.fetch_add(1);
    g_enabled_fast.store(true);g_config_generation.store(9);
    InitializeCriticalSection(&g_chain_cs);
}
}
void BoundaryNativeHooks() {
    using namespace boundary_native_test;
    using namespace nr_post_boundary;
    Initialize();
    auto *list=reinterpret_cast<ID3D12GraphicsCommandList *>(&object);
    auto *different=reinterpret_cast<ID3D12GraphicsCommandList *>(&other);
    auto *output=reinterpret_cast<ID3D12Resource *>(0x1010);
    auto *unrelated=reinterpret_cast<ID3D12Resource *>(0x5050);
    const auto coverage=EnsurePostBoundaryNativeHooks(list);
    Check((coverage&(Legacy|nr_post_boundary::Enhanced|Operations))==(Legacy|nr_post_boundary::Enhanced|Operations),"real MinHook base and CL7 observation batch published");
    Check(!(coverage&(HookMissing|InterfaceUnknown)),"optional native observation reports complete installed method family");
    Check(SnapshotPostBoundaryList(list).token==1001 && SnapshotPostBoundaryList(list).known,"native detours use real bounded command registry identity");
    expose_extended=false;Check((EnsurePostBoundaryNativeHooks(list)&EnhancedAbsent)!=0,"absent CL7 is reported explicitly");
    expose_extended=true;tear_off=true;Check((EnsurePostBoundaryNativeHooks(list)&InterfaceUnknown)!=0,"distinct CL7 interface cannot borrow base identity");tear_off=false;
    auto bad_table=table;bad_table.ResourceBarrier=nullptr;ID3D12GraphicsCommandList7 bad_object{&bad_table};
    const auto bypasses=g_command_state_bypasses.load();
    Check((EnsurePostBoundaryNativeHooks(reinterpret_cast<ID3D12GraphicsCommandList *>(&bad_object))&HookMissing)!=0 &&
          g_command_state_bypasses.load()==bypasses,"optional publication failure reports a gap without changing NR admission");
    NrNativeOutputTraceScope scope(output,list,coverage);
    g_inside_ngx_evaluate=true;
    D3D12_RESOURCE_BARRIER legacy[2]={};
    legacy[0].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;legacy[0].Flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    legacy[0].Transition={output,7,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    legacy[1]=legacy[0];legacy[1].Transition.pResource=unrelated;
    const auto original=legacy[0];const auto before_calls=forwarded;
    ID3D12GraphicsCommandList_ResourceBarrier(list,2,legacy);
    Check(forwarded==before_calls+1 && last_pointer==legacy && last_count==2 && !std::memcmp(&original,&legacy[0],sizeof(original)),"legacy barrier forwarded once with unchanged array/count/flags/subresource");
    Check(scope.evidence.exact_matches==1 && scope.evidence.count==1,"exact target only; unrelated resource does not provide state");
    Check(scope.evidence.events[0].subresources[0]==7 && scope.evidence.events[0].flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY &&
          scope.evidence.events[0].before==D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"legacy native record retains subresource and split flag");
    ID3D12GraphicsCommandList_ResourceBarrier(different,1,legacy);
    Check(scope.evidence.exact_other_lists==1 && scope.evidence.exact_matches==1,"different native list cannot supply SR exit state");
    {
        NrNativeOutputTraceScope nested(unrelated,different,coverage);
        ID3D12GraphicsCommandList_ResourceBarrier(different,1,&legacy[1]);
        Check(nested.evidence.exact_matches==1 && scope.evidence.exact_matches==1,"nested native hook scopes remain isolated");
    }
    D3D12_TEXTURE_BARRIER texture={};texture.pResource=output;
    texture.SyncBefore=D3D12_BARRIER_SYNC_SPLIT;texture.SyncAfter=D3D12_BARRIER_SYNC_PIXEL_SHADING;
    texture.AccessBefore=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;texture.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    texture.LayoutBefore=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;texture.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    texture.Subresources={2,3,4,5,1,2};texture.Flags=D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
    D3D12_BARRIER_GROUP group={};group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&texture;
    ID3D12GraphicsCommandList7_Barrier(&object,1,&group);
    Check(last_pointer==&group && last_count==1,"enhanced Barrier forwards exact group pointer");
    const auto &enhanced=scope.evidence.events[1];
    Check(enhanced.kind==Kind::EnhancedTexture && enhanced.subresources[0]==2 && enhanced.subresources[5]==2 &&
          enhanced.sync_before==static_cast<unsigned>(D3D12_BARRIER_SYNC_SPLIT) && enhanced.after==D3D12_BARRIER_LAYOUT_SHADER_RESOURCE &&
          enhanced.flags==D3D12_TEXTURE_BARRIER_FLAG_DISCARD,"CL7 exact six-field range/layout/access/split-sync retained");
    D3D12_GLOBAL_BARRIER global={D3D12_BARRIER_SYNC_ALL,D3D12_BARRIER_SYNC_COPY,D3D12_BARRIER_ACCESS_COMMON,D3D12_BARRIER_ACCESS_COPY_SOURCE};
    group.Type=D3D12_BARRIER_TYPE_GLOBAL;group.pGlobalBarriers=&global;ID3D12GraphicsCommandList7_Barrier(&object,1,&group);
    Check(scope.evidence.exact_global==1 && !scope.evidence.events[2].target_known,"global barrier is a global observation, not target state");
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    Check(scope.evidence.unknown_operations==1 && scope.evidence.count==3,"native dispatch does not invent texture descriptor contents");
    const auto native=scope.Finish();g_inside_ngx_evaluate=false;
    NrDiagnosticIdentity identity{};identity.frame_id=71;identity.config_generation=9;
    const ObservationScope store_scope{17,3,101,1,9,1,0,0};
    const auto trace=PublishPostBoundary(output,native,identity,3,0x4444,store_scope,Provenance::StoreProbe);
    ID3D12GraphicsCommandList_CopyResource(list,unrelated,output);
    UINT zeros[4]={};FLOAT floats[4]={};
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list,{}, {},output,zeros,0,nullptr);
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list,{}, {},unrelated,floats,0,nullptr);
    ID3D12GraphicsCommandList_DrawInstanced(list,3,1,0,0);
    for(unsigned i=0;i<20;++i)ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    Check(g_post_boundary_current.count==4,"target copy/clear and bounded unresolved draw/dispatch retained; unrelated UAV clear excluded");
    Check(g_post_boundary_current.events[0].reads && g_post_boundary_current.events[0].target_known &&
          g_post_boundary_current.events[1].writes && g_post_boundary_current.events[1].target_known,"explicit source/destination APIs prove operation target");
    Check(!g_post_boundary_current.events[2].target_known && !g_post_boundary_current.events[3].target_known &&
          g_post_boundary_current.unknown_operations==21,"draw/dispatch stay unresolved; repeated unknown operations counted without evicting known target events");
    g_inside_ngx_evaluate=true;ID3D12GraphicsCommandList_CopyResource(list,output,unrelated);g_inside_ngx_evaluate=false;
    Check(g_post_boundary_current.count==4,"plugin-internal commands after native scope are excluded");
    auto *queue=reinterpret_cast<ID3D12CommandQueue *>(0x7770);
    g_graphics_queues[0]={queue,77,true};g_original_queue_execute[0]=Execute;g_lifecycle_active_command_lists.store(0);
    ID3D12CommandList *lists[]={reinterpret_cast<ID3D12CommandList *>(list)};
    ProcessSubmittedCommandLists(0,queue,1,lists);
    Check(execute_returns==1 && last_pointer==lists,"ordinary list is forwarded unchanged even without NR lifecycle batch");
    Check(g_post_boundary_current.origin_submitted && g_post_boundary_current.origin_queue==77 &&
          g_post_boundary_current.events[0].submitted,"real Execute-return hook associates origin and following command tokens");
    ClosePostBoundaryAtPresent(0x4444);const auto closed=SnapshotPostBoundary(trace,71,9,3);
    Check(closed.trace==trace && closed.close==Close::Present && closed.count==4,"same probe trace closes as immutable record");
    ID3D12GraphicsCommandList_CopyResource(list,output,unrelated);ClosePostBoundaryAtPresent(0x4444);
    Check(SnapshotPostBoundary(trace,71,9,3).count==4,"native hooks cannot append after first Present");
    identity.frame_id=72;PublishPostBoundary(output,native,identity,3,0x4444);
    for(unsigned i=0;i<kFollowingEvents+3;++i)ID3D12GraphicsCommandList_CopyResource(list,output,unrelated);
    ClosePostBoundaryAtPresent(0x4444);
    Check(SnapshotPostBoundary().count==kFollowingEvents && SnapshotPostBoundary().dropped==3 &&
          (SnapshotPostBoundary().coverage&EventLoss),"recording saturation is bounded and explicit, never a complete-state claim");
    // The record address is reused, but old token/reset evidence cannot attach
    // a fresh submission receipt to the old recording.
    identity.frame_id=73;PublishPostBoundary(output,native,identity,3,0x4444);
    g_command_states[0].token=2001;g_command_state_epoch.fetch_add(1);
    ObservePostBoundarySubmission(SnapshotPostBoundaryList(list),77,999);
    Check(!g_post_boundary_current.origin_submitted,"reused command address cannot inherit prior identity's submission");
    ClosePostBoundaryAtPresent(0x4444);
    g_post_store_evidence.frame=71;g_post_store_evidence.config=9;g_post_store_evidence.feature_generation=3;
    g_post_store_evidence.boundary_trace_id=trace;g_post_store_evidence.samples=2040;
    g_post_store_evidence.mode=1;g_post_store_evidence.recovery_token.key.scope.device=17;
    g_post_store_evidence.recovery_target={101,101,0};
    auto feedback=std::make_unique<FeedbackSession>();
    FeedbackAppendActivity(*feedback,"start");FeedbackAppendActivity(*feedback,"end");
    Check(!feedback->report_truncated && std::strstr(feedback->report,"end.post.boundary.pairedWithProbe=1") &&
          std::strstr(feedback->report,"end.post.boundary.native.0=") && !std::strstr(feedback->report,"start.post.boundary.native.0="),
          "actual F8 writer exports one matched bounded event window without duplicating start events");
    g_post_store_evidence.boundary_trace_id=trace+1000;feedback=std::make_unique<FeedbackSession>();
    FeedbackAppendActivity(*feedback,"end");
    Check(std::strstr(feedback->report,"end.post.boundary.pairedWithProbe=0") && !std::strstr(feedback->report,"end.post.boundary.native.0="),
          "missing exact probe trace never borrows a nearby historical window");
    identity.frame_id=74;const auto full_trace=PublishPostBoundary(output,native,identity,3,0x4444,store_scope,Provenance::StoreProbe);
    Event wide;wide.kind=Kind::EnhancedTexture;wide.list.token=wide.list.reset=wide.order=wide.queue=wide.submission=UINT64_MAX;
    wide.before=wide.after=wide.sync_before=wide.sync_after=wide.access_before=wide.access_after=UINT64_MAX;
    wide.flags=UINT_MAX;for(auto &value:wide.subresources)value=UINT_MAX;
    wide.list.known=wide.target_known=wide.reads=wide.writes=wide.submitted=true;
    g_post_boundary_current.native.count=kNativeEvents;g_post_boundary_current.count=kFollowingEvents;
    for(auto &event:g_post_boundary_current.native.events)event=wide;
    for(auto &event:g_post_boundary_current.events)event=wide;
    ClosePostBoundaryAtPresent(0x4444);
    g_post_store_evidence.frame=74;g_post_store_evidence.boundary_trace_id=full_trace;
    g_command_history.count=kCommandHistoryLimit;
    for(auto &entry:g_command_history.entries){entry.token=UINT64_MAX;entry.available=true;entry.reason="unobserved";}
    feedback=std::make_unique<FeedbackSession>();
    const std::string other_metadata(6000,'x');FeedbackAppend(*feedback,"%s\n",other_metadata.c_str());
    FeedbackAppendActivity(*feedback,"start");FeedbackAppendActivity(*feedback,"end");FeedbackAppendCommandHistory(*feedback);
    FeedbackAppend(*feedback,"boundary.test.requiredTail=present\n");
    Check(!feedback->report_truncated && std::strstr(feedback->report,"boundary.test.requiredTail=present"),
          "full-width bounded events plus both snapshots/history leave F8 metadata tail intact");
    std::printf("Boundary F8 budget: %zu/%zu bytes including 6000 bytes of other metadata and full bounded histories\n",feedback->report_used,sizeof(feedback->report));
    std::printf("Boundary native: %u checks; actual production detours/MinHook, synthetic COM ABI, CPU only; no WARP/NGX/game\n",queue_test::checks);
}
