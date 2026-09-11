// Included after the existing command-state implementation. Optional diagnostic
// families share its MinHook publication/owner pinning, never its admission result.
// These ABI declarations come directly from the compiler's D3D12 SDK header.
static_assert(offsetof(ID3D12GraphicsCommandListVtbl,ResourceBarrier)==26*sizeof(void *));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Barrier)==80*sizeof(void *));
static_assert(sizeof(D3D12_BARRIER_SUBRESOURCE_RANGE)==6*sizeof(UINT));
static_assert(sizeof(D3D12_BARRIER_GROUP)==16);
static_assert(offsetof(D3D12_TEXTURE_BARRIER,Subresources)==32);

nr_post_boundary::List SnapshotPostBoundaryList(ID3D12GraphicsCommandList *native) {
    nr_post_boundary::List result;result.address=reinterpret_cast<std::uint64_t>(native);
    if(!native)return result;
    // Preserve the native fast path: recording hooks read only value records.
    thread_local ID3D12GraphicsCommandList *last_native=nullptr;
    thread_local std::uint64_t last_epoch=0;
    thread_local CommandStateRecord *last_record=nullptr;
    AcquireSRWLockShared(&g_command_state_lock);
    const auto epoch=g_command_state_epoch.load(std::memory_order_relaxed);
    if(last_native!=native || last_epoch!=epoch) {
        last_record=nullptr;
        for(auto &entry:g_command_states)if(entry.native==native && entry.token) {last_record=&entry;break;}
        last_native=native;last_epoch=epoch;
    }
    const auto *record=last_record;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    // Proxy mappings are cold-verified, bounded by an actual original call,
    // and invalidated by record retirement/replacement. Never read COM here.
    unsigned alias_depth=0;
    if(!record)for(auto *alias=g_nr_post_boundary_alias_scope;alias && alias_depth++<16;alias=alias->parent) {
        if(alias->original!=result.address)continue;
        if(alias->epoch!=epoch || !alias->identity.known)break;
        const auto *candidate=FindCommandStateTokenLocked(alias->identity.token);
        if(candidate && reinterpret_cast<std::uint64_t>(candidate->native)==alias->identity.address &&
           candidate->native_resets==alias->identity.reset && candidate->meta_contract.device_identity==alias->identity.device)
            record=candidate;
        break;
    }
#endif
    if(record && record->native && record->lifetime_ready) {
        result.address=reinterpret_cast<std::uint64_t>(record->native);
        result.token=record->token;
        result.reset=record->native_resets;
        result.device=record->meta_contract.device_identity;
        result.known=result.device!=0;
    }
    ReleaseSRWLockShared(&g_command_state_lock);
    return result;
}
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
nr_post_boundary::List ResolvePostBoundaryListForObservation(ID3D12GraphicsCommandList *list) {
    nr_post_boundary::List result;result.address=reinterpret_cast<std::uint64_t>(list);
    if(!list)return result;
    // Exact original pointer, exact attached lifetime interface, current record.
    // Only this cold boundary may call private data/GetDevice for an alias.
    const auto token=ReadCommandStateToken(list);
    IUnknown *object=nullptr;UINT bytes=sizeof(object);
    if(!token || FAILED(ID3D12GraphicsCommandList_GetPrivateData(list,kNativeCommandLifetime,&bytes,&object)) ||
       bytes!=sizeof(object) || !object)return result;
    const auto *life=reinterpret_cast<const NativeCommandLifetime *>(object);
    const bool attached=object->lpVtbl==&kNativeCommandLifetimeVtable && life->armed && life->token==token && life->native;
    if(attached) {
        ID3D12Device *device=nullptr,*native_device=nullptr;
        std::uint64_t device_identity=0;
        if(SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void **>(&device))) && device) {
            CommandStateMetaContract meta;UINT meta_bytes=sizeof(meta);
            if(SUCCEEDED(ID3D12Device_GetPrivateData(device,kCommandStateMetaContract,&meta_bytes,&meta)) &&
               meta_bytes==sizeof(meta))device_identity=meta.device_identity;
            bool same=false;
            if(SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(life->native,IID_ID3D12Device,
                    reinterpret_cast<void **>(&native_device))) && native_device) {
                same=device==native_device;
                if(!same) {
                    // Command-state and native-format IDs are separate domains.
                    // Match command metadata only after an exact COM lifetime
                    // proof connects the two current device interfaces.
                    const auto format_device=NrFormatDeviceIdentity(device);
                    const auto descriptor_life=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(device),
                        NrDescriptorObject::Device,format_device);
                    same=format_device && format_device==NrFormatDeviceIdentity(native_device) && descriptor_life &&
                        descriptor_life==NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(native_device),
                            NrDescriptorObject::Device,format_device);
                }
                ID3D12Device_Release(native_device);
            }
            if(!same)device_identity=0;
            ID3D12Device_Release(device);
        }
        AcquireSRWLockShared(&g_command_state_lock);
        const auto *record=FindCommandStateTokenLocked(token);
        if(record && record->native==life->native && record->native_created && record->lifetime_ready &&
           device_identity && record->meta_contract.device_identity==device_identity) {
            result={reinterpret_cast<std::uint64_t>(record->native),record->token,record->native_resets,device_identity,true};
        }
        ReleaseSRWLockShared(&g_command_state_lock);
    }
    IUnknown_Release(object);return result;
}
NrPostBoundaryAliasScope::NrPostBoundaryAliasScope(ID3D12GraphicsCommandList *list) {
    parent=g_nr_post_boundary_alias_scope;original=reinterpret_cast<std::uint64_t>(list);
    // Depth is bounded without a persistent alias-address cache. An excessive
    // nested call simply has no alias evidence; its original still runs once.
    unsigned depth=0;for(auto *outer=parent;outer;outer=outer->parent)if(++depth>=16)break;
    const auto before=g_command_state_epoch.load(std::memory_order_acquire);
    if(depth<16)identity=ResolvePostBoundaryListForObservation(list);
    epoch=g_command_state_epoch.load(std::memory_order_acquire);
    if(before!=epoch)identity.known=false;
    g_nr_post_boundary_alias_scope=this;
}
NrPostBoundaryAliasScope::~NrPostBoundaryAliasScope(){g_nr_post_boundary_alias_scope=parent;}
#endif
std::uint64_t PostBoundaryTarget() {
    return g_native_output_trace ? g_native_output_trace->resource :
        g_post_boundary_resource.load(std::memory_order_acquire);
}
nr_post_boundary::Event PostBoundaryEvent(ID3D12GraphicsCommandList *list,nr_post_boundary::Kind kind) {
    nr_post_boundary::Event event;event.kind=kind;event.list=SnapshotPostBoundaryList(list);return event;
}
void ObservePostBoundaryLegacy(ID3D12GraphicsCommandList *list,UINT count,const D3D12_RESOURCE_BARRIER *barriers) {
    if(!PostBoundaryRecordingActive())return;
    if(!barriers&&count){MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);return;}
    if(count>256)MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);
    const auto target=PostBoundaryTarget();
    for(UINT i=0;i<std::min(count,256u);++i) {
        const auto &barrier=barriers[i];
        auto event=PostBoundaryEvent(list,nr_post_boundary::Kind::Transition);
        event.flags=static_cast<unsigned>(barrier.Flags);
        std::uint64_t resource=0;
        if(barrier.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            resource=reinterpret_cast<std::uint64_t>(barrier.Transition.pResource);
            event.before=static_cast<unsigned>(barrier.Transition.StateBefore);
            event.after=static_cast<unsigned>(barrier.Transition.StateAfter);
            event.subresources[0]=barrier.Transition.Subresource;
        } else if(barrier.Type==D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            event.kind=nr_post_boundary::Kind::Uav;
            resource=reinterpret_cast<std::uint64_t>(barrier.UAV.pResource);
        } else if(barrier.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
            event.kind=nr_post_boundary::Kind::Aliasing;
            const auto before=reinterpret_cast<std::uint64_t>(barrier.Aliasing.pResourceBefore);
            const auto after=reinterpret_cast<std::uint64_t>(barrier.Aliasing.pResourceAfter);
            if(before==target||after==target)resource=target;
            else if(before||after)continue;
            // Retain which side matched without leaking raw resource addresses.
            event.before=before==target;event.after=after==target;
        } else {MarkPostBoundaryObservationGap(nr_post_boundary::OpaqueOperation);continue;}
        if(resource && resource!=target)continue;
        ObservePostBoundaryEvent(event,resource);
    }
}
void ObservePostBoundaryEnhanced(ID3D12GraphicsCommandList *list,UINT count,const D3D12_BARRIER_GROUP *groups) {
    if(!PostBoundaryRecordingActive())return;
    if(!groups&&count){MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);return;}
    if(count>64)MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);
    const auto target=PostBoundaryTarget();
    unsigned remaining=256;
    for(UINT i=0;i<std::min(count,64u);++i) {
        const auto &group=groups[i];
        const unsigned take=std::min(group.NumBarriers,remaining);
        if(take<group.NumBarriers)MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);
        remaining-=take;
        if(group.NumBarriers&&!group.pGlobalBarriers) {MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss);continue;}
        for(unsigned j=0;j<take;++j) {
            auto event=PostBoundaryEvent(list,nr_post_boundary::Kind::EnhancedTexture);
            std::uint64_t resource=0;
            if(group.Type==D3D12_BARRIER_TYPE_TEXTURE) {
                const auto &b=group.pTextureBarriers[j];
                resource=reinterpret_cast<std::uint64_t>(b.pResource);
                event.before=static_cast<unsigned>(b.LayoutBefore);event.after=static_cast<unsigned>(b.LayoutAfter);
                event.sync_before=static_cast<std::uint64_t>(b.SyncBefore);event.sync_after=static_cast<std::uint64_t>(b.SyncAfter);
                event.access_before=static_cast<std::uint64_t>(b.AccessBefore);event.access_after=static_cast<std::uint64_t>(b.AccessAfter);
                std::memcpy(event.subresources,&b.Subresources,sizeof(event.subresources));
                event.flags=static_cast<unsigned>(b.Flags);
            } else if(group.Type==D3D12_BARRIER_TYPE_GLOBAL) {
                const auto &b=group.pGlobalBarriers[j];event.kind=nr_post_boundary::Kind::EnhancedGlobal;
                event.sync_before=static_cast<std::uint64_t>(b.SyncBefore);event.sync_after=static_cast<std::uint64_t>(b.SyncAfter);
                event.access_before=static_cast<std::uint64_t>(b.AccessBefore);event.access_after=static_cast<std::uint64_t>(b.AccessAfter);
            } else if(group.Type==D3D12_BARRIER_TYPE_BUFFER) {
                const auto &b=group.pBufferBarriers[j];event.kind=nr_post_boundary::Kind::EnhancedBuffer;
                resource=reinterpret_cast<std::uint64_t>(b.pResource);
                event.before=b.Offset;event.after=b.Size;
                event.sync_before=static_cast<std::uint64_t>(b.SyncBefore);event.sync_after=static_cast<std::uint64_t>(b.SyncAfter);
                event.access_before=static_cast<std::uint64_t>(b.AccessBefore);event.access_after=static_cast<std::uint64_t>(b.AccessAfter);
            } else {MarkPostBoundaryObservationGap(nr_post_boundary::OpaqueOperation);break;}
            if(resource&&resource!=target)continue;
            ObservePostBoundaryEvent(event,resource);
        }
    }
}
void ObservePostBoundaryOperation(ID3D12GraphicsCommandList *list,nr_post_boundary::Kind kind,
        ID3D12Resource *source=nullptr,ID3D12Resource *destination=nullptr,UINT source_subresource=0,UINT destination_subresource=0) {
    if(!PostBoundaryRecordingActive())return;
    const auto target=PostBoundaryTarget();
    const bool read=reinterpret_cast<std::uint64_t>(source)==target;
    const bool write=reinterpret_cast<std::uint64_t>(destination)==target;
    if((source||destination)&&!read&&!write)return;
    auto event=PostBoundaryEvent(list,kind);event.reads=read;event.writes=write;
    event.subresources[0]=source_subresource;event.subresources[1]=destination_subresource;
    ObservePostBoundaryEvent(event,(read||write)?target:0);
}
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
#include "nr_descriptor_provenance_commands.inl"
#endif
// Existing opaque/state hooks call this outside their state-registry lock.
// Root tables alone still do not establish the texture written by Draw/Dispatch.
void ObservePostBoundaryStateCommand(ID3D12GraphicsCommandList *list,const char *name) {
    if(!PostBoundaryRecordingActive())return;
#if !defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    if(std::strcmp(name,"OMSetRenderTargets")==0)
        ObservePostBoundaryOperation(list,nr_post_boundary::Kind::BindTargets);
    else
#endif
    if(std::strcmp(name,"ExecuteBundle")==0 || std::strcmp(name,"ExecuteIndirect")==0 ||
            std::strcmp(name,"BeginRenderPass")==0 || std::strcmp(name,"InitializeMetaCommand")==0 ||
            std::strcmp(name,"ExecuteMetaCommand")==0 || std::strcmp(name,"SetProgram")==0) {
        ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Opaque);
        MarkPostBoundaryObservationGap(nr_post_boundary::OpaqueOperation);
    }
}

#define NR_POST_NATIVE_HOOK(NAME,PARAMETERS,ARGUMENTS,OBSERVE) \
    CommandStateHookFamily g_post_hook_##NAME; \
    template<unsigned I> void STDMETHODCALLTYPE PostHook##NAME PARAMETERS { \
        using Fn=decltype(ID3D12GraphicsCommandListVtbl::NAME); \
        reinterpret_cast<Fn>(g_post_hook_##NAME.originals[I]) ARGUMENTS; \
        OBSERVE; \
    } \
    void *const g_post_detours_##NAME[4]={reinterpret_cast<void *>(&PostHook##NAME<0>), \
        reinterpret_cast<void *>(&PostHook##NAME<1>),reinterpret_cast<void *>(&PostHook##NAME<2>),reinterpret_cast<void *>(&PostHook##NAME<3>)};

NR_POST_NATIVE_HOOK(ResourceBarrier,(ID3D12GraphicsCommandList *list,UINT count,const D3D12_RESOURCE_BARRIER *barriers),
    (list,count,barriers),ObservePostBoundaryLegacy(list,count,barriers))
NR_POST_NATIVE_HOOK(CopyResource,(ID3D12GraphicsCommandList *list,ID3D12Resource *destination,ID3D12Resource *source),
    (list,destination,source),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Copy,source,destination))
NR_POST_NATIVE_HOOK(CopyTextureRegion,(ID3D12GraphicsCommandList *list,const D3D12_TEXTURE_COPY_LOCATION *destination,UINT x,UINT y,UINT z,const D3D12_TEXTURE_COPY_LOCATION *source,const D3D12_BOX *box),
    (list,destination,x,y,z,source,box),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::CopyRegion,source?source->pResource:nullptr,destination?destination->pResource:nullptr,
        source&&source->Type==D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX?source->SubresourceIndex:UINT_MAX,
        destination&&destination->Type==D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX?destination->SubresourceIndex:UINT_MAX))
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
#define NR_DESCRIPTOR_CLEAR_UAV(NAME,VALUE) \
    CommandStateHookFamily g_post_hook_##NAME; \
    template<unsigned I> void STDMETHODCALLTYPE PostHook##NAME(ID3D12GraphicsCommandList *list, \
            D3D12_GPU_DESCRIPTOR_HANDLE gpu,D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *resource, \
            const VALUE values[4],UINT count,const D3D12_RECT *rects) { \
        const bool observe=NrDescriptorObserveCommandLayer(list); \
        const auto descriptors=observe?NrDescriptorCaptureUav(list,gpu,cpu,resource):nr_descriptor::Pair{}; \
        using Fn=decltype(ID3D12GraphicsCommandListVtbl::NAME); \
        reinterpret_cast<Fn>(g_post_hook_##NAME.originals[I])(list,gpu,cpu,resource,values,count,rects); \
        if(observe)ObservePostBoundaryUavDescriptor(list,resource,descriptors, \
            NrDescriptorCaptureUav(list,gpu,cpu,resource),count,rects); \
    } \
    void *const g_post_detours_##NAME[4]={reinterpret_cast<void *>(&PostHook##NAME<0>), \
        reinterpret_cast<void *>(&PostHook##NAME<1>),reinterpret_cast<void *>(&PostHook##NAME<2>),reinterpret_cast<void *>(&PostHook##NAME<3>)};
NR_DESCRIPTOR_CLEAR_UAV(ClearUnorderedAccessViewUint,UINT)
NR_DESCRIPTOR_CLEAR_UAV(ClearUnorderedAccessViewFloat,FLOAT)
#undef NR_DESCRIPTOR_CLEAR_UAV
CommandStateHookFamily g_post_hook_ClearRenderTargetView;
template<unsigned I> void STDMETHODCALLTYPE PostHookClearRenderTargetView(ID3D12GraphicsCommandList *list,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu,const FLOAT values[4],UINT count,const D3D12_RECT *rects) {
    const bool observe=NrDescriptorObserveCommandLayer(list);
    const auto descriptor=observe?NrDescriptorCaptureRtv(cpu):nr_descriptor::Receipt{};
    using Fn=decltype(ID3D12GraphicsCommandListVtbl::ClearRenderTargetView);
    reinterpret_cast<Fn>(g_post_hook_ClearRenderTargetView.originals[I])(list,cpu,values,count,rects);
    if(observe)ObservePostBoundaryRtvDescriptor(list,nr_post_boundary::Kind::ClearRtv,descriptor,count,rects);
}
void *const g_post_detours_ClearRenderTargetView[4]={reinterpret_cast<void *>(&PostHookClearRenderTargetView<0>),
    reinterpret_cast<void *>(&PostHookClearRenderTargetView<1>),reinterpret_cast<void *>(&PostHookClearRenderTargetView<2>),reinterpret_cast<void *>(&PostHookClearRenderTargetView<3>)};
#else
NR_POST_NATIVE_HOOK(ClearUnorderedAccessViewUint,(ID3D12GraphicsCommandList *list,D3D12_GPU_DESCRIPTOR_HANDLE gpu,D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *resource,const UINT values[4],UINT count,const D3D12_RECT *rects),
    (list,gpu,cpu,resource,values,count,rects),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::ClearUav,nullptr,resource))
NR_POST_NATIVE_HOOK(ClearUnorderedAccessViewFloat,(ID3D12GraphicsCommandList *list,D3D12_GPU_DESCRIPTOR_HANDLE gpu,D3D12_CPU_DESCRIPTOR_HANDLE cpu,ID3D12Resource *resource,const FLOAT values[4],UINT count,const D3D12_RECT *rects),
    (list,gpu,cpu,resource,values,count,rects),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::ClearUav,nullptr,resource))
NR_POST_NATIVE_HOOK(ClearRenderTargetView,(ID3D12GraphicsCommandList *list,D3D12_CPU_DESCRIPTOR_HANDLE cpu,const FLOAT values[4],UINT count,const D3D12_RECT *rects),
    (list,cpu,values,count,rects),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::ClearRtv))
#endif
NR_POST_NATIVE_HOOK(DrawInstanced,(ID3D12GraphicsCommandList *list,UINT vertices,UINT instances,UINT start,UINT first_instance),
    (list,vertices,instances,start,first_instance),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Draw))
NR_POST_NATIVE_HOOK(DrawIndexedInstanced,(ID3D12GraphicsCommandList *list,UINT indices,UINT instances,UINT start,INT base,UINT first_instance),
    (list,indices,instances,start,base,first_instance),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::DrawIndexed))
NR_POST_NATIVE_HOOK(Dispatch,(ID3D12GraphicsCommandList *list,UINT x,UINT y,UINT z),
    (list,x,y,z),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Dispatch))
NR_POST_NATIVE_HOOK(ResolveSubresource,(ID3D12GraphicsCommandList *list,ID3D12Resource *destination,UINT destination_subresource,ID3D12Resource *source,UINT source_subresource,DXGI_FORMAT format),
    (list,destination,destination_subresource,source,source_subresource,format),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Resolve,source,destination,source_subresource,destination_subresource))
NR_POST_NATIVE_HOOK(DiscardResource,(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,const D3D12_DISCARD_REGION *region),
    (list,resource,region),ObservePostBoundaryOperation(list,nr_post_boundary::Kind::Discard,nullptr,resource))
#undef NR_POST_NATIVE_HOOK

CommandStateHookFamily g_post_hook_Barrier;
template<unsigned I> void STDMETHODCALLTYPE PostHookBarrier(ID3D12GraphicsCommandList7 *list,UINT count,const D3D12_BARRIER_GROUP *groups) {
    using Fn=decltype(ID3D12GraphicsCommandList7Vtbl::Barrier);
    reinterpret_cast<Fn>(g_post_hook_Barrier.originals[I])(list,count,groups);
    ObservePostBoundaryEnhanced(reinterpret_cast<ID3D12GraphicsCommandList *>(list),count,groups);
}
void *const g_post_detours_Barrier[4]={reinterpret_cast<void *>(&PostHookBarrier<0>),
    reinterpret_cast<void *>(&PostHookBarrier<1>),reinterpret_cast<void *>(&PostHookBarrier<2>),reinterpret_cast<void *>(&PostHookBarrier<3>)};

struct PostBoundaryPublication {const ID3D12GraphicsCommandListVtbl *vtable=nullptr;unsigned coverage=0;};
PostBoundaryPublication g_post_boundary_publications[4];
SRWLOCK g_post_boundary_publication_lock=SRWLOCK_INIT;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
PostBoundaryPublication g_descriptor_command_publications[4];
bool EnsureNrDescriptorCommandHooks(ID3D12GraphicsCommandList *native) {
    if(!native)return false;
    AcquireSRWLockExclusive(&g_post_boundary_publication_lock);
    PostBoundaryPublication *entry=nullptr;
    for(auto &value:g_descriptor_command_publications)if(value.vtable==native->lpVtbl){entry=&value;break;}
    if(!entry)for(auto &value:g_descriptor_command_publications)if(!value.vtable){entry=&value;break;}
    if(entry && !entry->vtable) {
        entry->vtable=native->lpVtbl;
        CommandStateHookRequest requests[]={
            {&g_post_hook_ClearUnorderedAccessViewUint,reinterpret_cast<void *>(native->lpVtbl->ClearUnorderedAccessViewUint),g_post_detours_ClearUnorderedAccessViewUint},
            {&g_post_hook_ClearUnorderedAccessViewFloat,reinterpret_cast<void *>(native->lpVtbl->ClearUnorderedAccessViewFloat),g_post_detours_ClearUnorderedAccessViewFloat},
            {&g_post_hook_ClearRenderTargetView,reinterpret_cast<void *>(native->lpVtbl->ClearRenderTargetView),g_post_detours_ClearRenderTargetView}};
        entry->coverage=PublishCommandStateHooks(requests,static_cast<unsigned>(std::size(requests)),
            "optional descriptor-use observer incomplete; format admission unchanged")?1:0;
    }
    const bool ready=entry && entry->coverage;
    ReleaseSRWLockExclusive(&g_post_boundary_publication_lock);return ready;
}
bool NrDescriptorCommandHooksReady(ID3D12GraphicsCommandList *native) {
    if(!native)return false;
    bool ready=false;
    AcquireSRWLockShared(&g_post_boundary_publication_lock);
    for(const auto &entry:g_descriptor_command_publications)if(entry.vtable==native->lpVtbl){ready=entry.coverage!=0;break;}
    ReleaseSRWLockShared(&g_post_boundary_publication_lock);return ready;
}
#endif
unsigned EnsurePostBoundaryNativeHooks(ID3D12GraphicsCommandList *native) {
    using namespace nr_post_boundary;
    if(!native)return HookMissing|IdentityUnknown;
    unsigned coverage=0;
    // Cold, sampled carrier path only. Failed publications are remembered;
    // persistent failure must not suspend game threads on every one-shot list.
    AcquireSRWLockExclusive(&g_post_boundary_publication_lock);
    PostBoundaryPublication *entry=nullptr;
    for(auto &candidate:g_post_boundary_publications)if(candidate.vtable==native->lpVtbl){entry=&candidate;break;}
    if(!entry)for(auto &candidate:g_post_boundary_publications)if(!candidate.vtable){entry=&candidate;break;}
    if(entry && !entry->vtable) {
        entry->vtable=native->lpVtbl;
        CommandStateHookRequest requests[11];unsigned count=0;
#define NR_ADD_POST(NAME) requests[count++]={&g_post_hook_##NAME,reinterpret_cast<void *>(native->lpVtbl->NAME),g_post_detours_##NAME}
        NR_ADD_POST(ResourceBarrier);NR_ADD_POST(CopyResource);NR_ADD_POST(CopyTextureRegion);
        NR_ADD_POST(ClearUnorderedAccessViewUint);NR_ADD_POST(ClearUnorderedAccessViewFloat);NR_ADD_POST(ClearRenderTargetView);
        NR_ADD_POST(DrawInstanced);NR_ADD_POST(DrawIndexedInstanced);NR_ADD_POST(Dispatch);NR_ADD_POST(ResolveSubresource);NR_ADD_POST(DiscardResource);
#undef NR_ADD_POST
        entry->coverage=PublishCommandStateHooks(requests,count,"optional output observer incomplete; NR admission unchanged")?Legacy|Operations:HookMissing;
    }
    coverage=entry?entry->coverage:HookMissing;
    ReleaseSRWLockExclusive(&g_post_boundary_publication_lock);

    // Per-object QI is required: sharing a base vtable does not prove that a
    // newer interface is the same identity. A tear-off stays explicitly unknown.
    ID3D12GraphicsCommandList7 *extended=nullptr;
    const HRESULT result=ID3D12GraphicsCommandList_QueryInterface(native,IID_ID3D12GraphicsCommandList7,reinterpret_cast<void **>(&extended));
    if(result==E_NOINTERFACE&&!extended)return coverage|EnhancedAbsent;
    if(FAILED(result)||!extended)return coverage|InterfaceUnknown;
    if(reinterpret_cast<void *>(native)!=reinterpret_cast<void *>(extended)) {
        ID3D12GraphicsCommandList7_Release(extended);return coverage|InterfaceUnknown;
    }
    CommandStateHookRequest request{&g_post_hook_Barrier,reinterpret_cast<void *>(extended->lpVtbl->Barrier),g_post_detours_Barrier};
    // A separate bounded target attempt list shares the same publisher; no
    // addition to the 48-slot state-restoration request array is needed.
    static void *attempts[4]={};
    static unsigned attempt_count=0;
    AcquireSRWLockExclusive(&g_post_boundary_publication_lock);
    bool fresh=attempt_count<4;
    for(unsigned i=0;i<attempt_count;++i)if(attempts[i]==request.target)fresh=false;
    if(fresh){attempts[attempt_count++]=request.target;PublishCommandStateHooks(&request,1,"optional output observer incomplete; NR admission unchanged");}
    ReleaseSRWLockExclusive(&g_post_boundary_publication_lock);
    AcquireSRWLockShared(&g_evaluate_hook_lock);
    bool ready=false;
    for(unsigned i=0;i<g_post_hook_Barrier.count;++i)
        if(g_post_hook_Barrier.targets[i]==request.target&&g_post_hook_Barrier.enabled[i])ready=true;
    ReleaseSRWLockShared(&g_evaluate_hook_lock);
    ID3D12GraphicsCommandList7_Release(extended);
    return coverage|(ready?Enhanced:HookMissing);
}

std::atomic<std::uint64_t> g_post_boundary_submissions{0};
struct NrPostBoundarySubmitScope {
    ID3D12CommandQueue *queue;
    UINT count;
    ID3D12CommandList *const *lists;
    std::uint64_t identity=0,submission=0;
    NrPostBoundarySubmitScope(ID3D12CommandQueue *q,UINT n,ID3D12CommandList *const *v):queue(q),count(n),lists(v) {
        if(!queue||!lists)return;
        bool observe=g_post_boundary_resource.load(std::memory_order_acquire)!=0;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
        observe=observe || NrDescriptorHasPendingUses();
#endif
        if(!observe)return;
        EnterCriticalSection(&g_chain_cs);
        for(const auto &entry:g_graphics_queues)if(entry.live&&entry.queue==queue){identity=entry.identity;break;}
        LeaveCriticalSection(&g_chain_cs);
        submission=g_post_boundary_submissions.fetch_add(1,std::memory_order_relaxed)+1;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
        // Descriptor revisions are checked at the intercepted Execute entry.
        // Volatile slots may have changed since native Evaluate returned. This
        // remains a CPU receipt and does not authorize NR or prove GPU usage.
        for(UINT i=0;i<std::min(count,256u);++i)if(lists[i])
            NrDescriptorObserveSubmission(SnapshotPostBoundaryList(reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i])),identity,submission,i,count);
#endif
    }
    ~NrPostBoundarySubmitScope() {
        if(!submission)return;
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
        NrDescriptorExecuteReturned(submission);
#endif
        if(!g_post_boundary_resource.load(std::memory_order_acquire))return;
        if(count>256)MarkPostBoundaryObservationGap(nr_post_boundary::EventLoss|nr_post_boundary::SubmissionUnknown);
        for(UINT i=0;i<std::min(count,256u);++i)if(lists[i])
            ObservePostBoundarySubmission(SnapshotPostBoundaryList(reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i])),identity,submission,i,count);
    }
};
