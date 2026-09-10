// Included after NrNativeFormatScope. All COM calls are outside the value
// registry's mutex. Lifetime notifications own metadata only, never the device,
// heap or resource whose destruction they report.
nr_descriptor::Registry<> g_nr_descriptors;
extern std::atomic<unsigned long long> g_carrier_evaluations;
std::mutex g_nr_descriptor_identity_mutex;
std::atomic<std::uint64_t> g_nr_descriptor_ids{1},g_nr_descriptor_lifetime_failures{0};
constexpr GUID kNrDescriptorLifetime={0x288c1dbd,0x8a61,0x4aa4,{0x9c,0xb2,0x64,0xba,0xf1,0x54,0x66,0xa1}};
constexpr GUID kNrDescriptorDeviceHooks={0x288c1dbe,0x8a61,0x4aa4,{0x9c,0xb2,0x64,0xba,0xf1,0x54,0x66,0xa1}};
enum class NrDescriptorObject : unsigned { Device,Resource,Heap };
struct NrDescriptorLifetime {
    IUnknown iface;
    std::atomic<ULONG> references{1};
    nr_descriptor::Id id=0,device=0,address=0,semantic_resource=0;
    NrDescriptorObject kind=NrDescriptorObject::Resource;
    bool armed=false;
};
bool PinHookAddress(const void *,const char *,HMODULE);
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
void NrRouteRetireResource(std::uint64_t);
void NrRouteRetireDevice(std::uint64_t);
#endif
ULONG STDMETHODCALLTYPE NrDescriptorLifetimeAddRef(IUnknown *object) {
    return reinterpret_cast<NrDescriptorLifetime *>(object)->references.fetch_add(1,std::memory_order_relaxed)+1;
}
HRESULT STDMETHODCALLTYPE NrDescriptorLifetimeQuery(IUnknown *object,REFIID iid,void **out) {
    if(!out)return E_POINTER;
    *out=nullptr;if(!IsEqualGUID(iid,IID_IUnknown))return E_NOINTERFACE;
    *out=object;NrDescriptorLifetimeAddRef(object);return S_OK;
}
ULONG STDMETHODCALLTYPE NrDescriptorLifetimeRelease(IUnknown *object) {
    auto *value=reinterpret_cast<NrDescriptorLifetime *>(object);
    const auto remaining=value->references.fetch_sub(1,std::memory_order_acq_rel)-1;
    if(!remaining) {
        if(value->armed) {
            if(value->kind==NrDescriptorObject::Heap)g_nr_descriptors.RetireHeap(value->id);
            else if(value->kind==NrDescriptorObject::Device) {
                g_nr_descriptors.RetireDevice(value->device);
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
                NrRouteRetireDevice(value->device);
#endif
            }
            else {
                g_nr_descriptors.RetireResource(value->id);
                ForgetPostBoundaryDescriptorResource(value->address,value->id);
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
                NrRouteRetireResource(value->semantic_resource);
#endif
            }
        }
        delete value;
    }
    return remaining;
}
IUnknownVtbl kNrDescriptorLifetimeVtable={NrDescriptorLifetimeQuery,NrDescriptorLifetimeAddRef,NrDescriptorLifetimeRelease};
nr_descriptor::Id NrDescriptorObjectIdentity(ID3D12Object *object,NrDescriptorObject kind,
        nr_descriptor::Id device,nr_descriptor::Id semantic_resource=0) {
    if(!object || !device)return 0;
    static const bool pinned=PinHookAddress(reinterpret_cast<const void *>(&NrDescriptorLifetimeRelease),
        "descriptor evidence lifetime",nullptr);
    if(!pinned)return 0;
    // Cold per-object publication is serialized separately from value records.
    // SetPrivateDataInterface may release an old notification, so it must never
    // run under the value-registry mutex or a command-state lock.
    std::lock_guard identity_guard(g_nr_descriptor_identity_mutex);
    IUnknown *existing=nullptr;UINT bytes=sizeof(existing);
    if(SUCCEEDED(ID3D12Object_GetPrivateData(object,kNrDescriptorLifetime,&bytes,&existing)) &&
       bytes==sizeof(existing) && existing) {
        // The AddRef'd interface is attached to this COM lifetime. Wrappers may
        // forward it while exposing a different object address and CPU domain.
        const auto *receipt=reinterpret_cast<const NrDescriptorLifetime *>(existing);
        const auto id=existing->lpVtbl==&kNrDescriptorLifetimeVtable && receipt->armed && receipt->kind==kind && receipt->device==device &&
            (kind!=NrDescriptorObject::Resource || receipt->semantic_resource==semantic_resource)?receipt->id:0;
        IUnknown_Release(existing);return id;
    }
    auto *receipt=new(std::nothrow) NrDescriptorLifetime;
    if(!receipt){g_nr_descriptor_lifetime_failures.fetch_add(1,std::memory_order_relaxed);return 0;}
    receipt->iface.lpVtbl=&kNrDescriptorLifetimeVtable;
    receipt->id=g_nr_descriptor_ids.fetch_add(1,std::memory_order_relaxed);
    receipt->device=device;receipt->address=reinterpret_cast<nr_descriptor::Id>(object);receipt->kind=kind;
    receipt->semantic_resource=semantic_resource;
    const bool attached=SUCCEEDED(ID3D12Object_SetPrivateDataInterface(object,kNrDescriptorLifetime,&receipt->iface));
    receipt->armed=attached;
    const auto id=attached?receipt->id:0;
    NrDescriptorLifetimeRelease(&receipt->iface);
    if(!id)g_nr_descriptor_lifetime_failures.fetch_add(1,std::memory_order_relaxed);
    return id;
}
struct NrDescriptorDeviceStatus {
    nr_descriptor::Id device=0;
    bool ready=false;
    // Only the ReShade init callback's documented get_native() can name this
    // collector. Shared private data does not certify another interface layer.
    nr_descriptor::Id native_address=0;
};
NrDescriptorDeviceStatus NrDescriptorDeviceCoverage(ID3D12Device *device) {
    NrDescriptorDeviceStatus result;UINT bytes=sizeof(result);
    if(!device || FAILED(ID3D12Device_GetPrivateData(device,kNrDescriptorDeviceHooks,&bytes,&result)) ||
       bytes!=sizeof(result))return {};
    return result;
}
bool NrDescriptorObserveDeviceLayer(ID3D12Device *device) {
    const auto coverage=NrDescriptorDeviceCoverage(device);
    return coverage.ready && coverage.native_address==reinterpret_cast<nr_descriptor::Id>(device);
}
nr_descriptor::Resource NrDescriptorTrackResource(ID3D12Device *device,ID3D12Resource *resource) {
    if(!device || !resource)return {};
    const auto device_id=NrFormatDeviceIdentity(device);
    if(!device_id)return {};
    ID3D12Device *owner=nullptr;
    if(FAILED(ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void **>(&owner))) || !owner)return {};
    const bool same=NrFormatDeviceIdentity(owner)==device_id;
    ID3D12Device_Release(owner);
    if(!same)return {};
    const auto semantic_resource=NrFormatResourceIdentity(resource);
    if(!semantic_resource)return {};
    const auto id=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(resource),NrDescriptorObject::Resource,device_id,semantic_resource);
    const nr_descriptor::Resource value{id,device_id,reinterpret_cast<nr_descriptor::Id>(resource)};
    // Identity checks never publish a possibly short-lived wrapper address.
    // Only an actual canonical native CreateView event records that index.
    return value;
}
bool NrDescriptorTrackRouteResource(ID3D12Resource *resource,std::uint64_t semantic_device,
        std::uint64_t semantic_resource) {
    if(!resource || !semantic_device || !semantic_resource || NrFormatResourceIdentity(resource)!=semantic_resource)return false;
    ID3D12Device *device=nullptr;
    if(FAILED(ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void **>(&device))) || !device)return false;
    bool ready=NrFormatDeviceIdentity(device)==semantic_device;
    if(ready)ready=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(device),NrDescriptorObject::Device,semantic_device)!=0;
    const auto identity=ready?NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(resource),
        NrDescriptorObject::Resource,semantic_device,semantic_resource):0;
    ID3D12Device_Release(device);return identity!=0;
}
void NrDescriptorTrackHeap(ID3D12Device *device,ID3D12DescriptorHeap *heap) {
    const auto coverage=NrDescriptorDeviceCoverage(device);
    if(!heap || !NrDescriptorObserveDeviceLayer(device))return;
    ID3D12Device *owner=nullptr;
    if(FAILED(ID3D12DescriptorHeap_GetDevice(heap,IID_ID3D12Device,reinterpret_cast<void **>(&owner))) || !owner)return;
    const bool same_device=NrFormatDeviceIdentity(owner)==coverage.device;
    ID3D12Device_Release(owner);
    if(!same_device)return;
    const auto desc=ID3D12DescriptorHeap_GetDesc(heap);
    if(desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_RTV)return;
    const auto id=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(heap),NrDescriptorObject::Heap,coverage.device);
    if(!id)return;
    const bool visible=(desc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)!=0;
    nr_descriptor::Heap value{id,coverage.device,reinterpret_cast<nr_descriptor::Id>(heap),
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap).ptr,
        visible?ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap).ptr:0,
        desc.NumDescriptors,ID3D12Device_GetDescriptorHandleIncrementSize(device,desc.Type),
        static_cast<unsigned>(desc.Type),visible};
    g_nr_descriptors.ObserveHeap(value);
}
nr_descriptor::View NrDescriptorView(ID3D12Device *device,ID3D12Resource *resource,
        nr_descriptor::Kind kind,unsigned format,unsigned mip,unsigned plane,bool shape) {
    nr_descriptor::View view;view.kind=kind;
    view.resource=NrDescriptorTrackResource(device,resource);
    view.known=static_cast<bool>(view.resource);
    if(!view.known)return view;
    if(NrDescriptorObserveDeviceLayer(device))g_nr_descriptors.ObserveResource(view.resource);
    const auto desc=ID3D12Resource_GetDesc(resource);
    view.format=format;view.mip=mip;view.plane=plane;
    view.typed=format && !nr_format_access::IsTypeless(format) && nr_format_access::LegalView(static_cast<unsigned>(desc.Format),format);
    view.shape_known=shape && desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize==1 &&
        desc.SampleDesc.Count==1 && mip<desc.MipLevels && mip<32 && plane==0 && desc.Width<=UINT_MAX;
    if(view.shape_known) {
        view.width=std::max(1u,static_cast<unsigned>(desc.Width)>>mip);
        view.height=std::max(1u,desc.Height>>mip);
    }
    return view;
}
nr_descriptor::Write NrDescriptorBeginSrv(ID3D12Device *device,ID3D12Resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    if(!NrDescriptorObserveDeviceLayer(device))return {};
    const bool shape=desc && desc->ViewDimension==D3D12_SRV_DIMENSION_TEXTURE2D &&
        desc->Shader4ComponentMapping==D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING &&
        desc->Texture2D.MipLevels==1 && desc->Texture2D.ResourceMinLODClamp==0;
    const auto view=NrDescriptorView(device,resource,nr_descriptor::Kind::Srv,desc?desc->Format:0,
        shape?desc->Texture2D.MostDetailedMip:0,shape?desc->Texture2D.PlaneSlice:0,shape);
    return g_nr_descriptors.BeginWrite(cpu.ptr,0,view);
}
nr_descriptor::Write NrDescriptorBeginUav(ID3D12Device *device,ID3D12Resource *resource,ID3D12Resource *counter,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    if(!NrDescriptorObserveDeviceLayer(device))return {};
    const bool shape=!counter && desc && desc->ViewDimension==D3D12_UAV_DIMENSION_TEXTURE2D;
    const auto view=NrDescriptorView(device,resource,nr_descriptor::Kind::Uav,desc?desc->Format:0,
        shape?desc->Texture2D.MipSlice:0,shape?desc->Texture2D.PlaneSlice:0,shape);
    return g_nr_descriptors.BeginWrite(cpu.ptr,0,view);
}
nr_descriptor::Write NrDescriptorBeginRtv(ID3D12Device *device,ID3D12Resource *resource,
        const D3D12_RENDER_TARGET_VIEW_DESC *desc,D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    if(!NrDescriptorObserveDeviceLayer(device))return {};
    const bool shape=desc && desc->ViewDimension==D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto view=NrDescriptorView(device,resource,nr_descriptor::Kind::Rtv,desc?desc->Format:0,
        shape?desc->Texture2D.MipSlice:0,shape?desc->Texture2D.PlaneSlice:0,shape);
    return g_nr_descriptors.BeginWrite(cpu.ptr,2,view);
}
// Small copies retain per-slot provenance. Larger copies mask their destination
// ranges for the complete native call and leave those destinations unknown.
// Unrelated views remain usable; observation limits are not native API limits.
struct NrDescriptorCopy {
    static constexpr unsigned kMaximum=256;
    std::unique_ptr<nr_descriptor::Write[]> writes;
    std::unique_ptr<nr_descriptor::UnknownRange[]> ranges;
    nr_descriptor::UnknownCopy unknown;
    unsigned count=0;
    ~NrDescriptorCopy(){
        for(unsigned i=0;i<count;++i)g_nr_descriptors.Finish(writes[i]);
        if(unknown.active)g_nr_descriptors.EndUnknownCopy(unknown);
    }
    NrDescriptorCopy()=default;
    NrDescriptorCopy(const NrDescriptorCopy&)=delete;
    NrDescriptorCopy& operator=(const NrDescriptorCopy&)=delete;
};
void NrDescriptorBeginCopy(NrDescriptorCopy& copy,ID3D12Device *device,UINT destinations,
        const D3D12_CPU_DESCRIPTOR_HANDLE *destination,const UINT *destination_sizes,UINT sources,
        const D3D12_CPU_DESCRIPTOR_HANDLE *source,const UINT *source_sizes,D3D12_DESCRIPTOR_HEAP_TYPE type) {
    if(!NrDescriptorObserveDeviceLayer(device) || (type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && type!=D3D12_DESCRIPTOR_HEAP_TYPE_RTV))return;
    const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,type);
    const auto unknown_destinations=[&]() {
        // This small RAII object exists even if either allocation fails. Unknown
        // geometry then requires a global mask, including the native-call window.
        if(!stride || (destinations&&!destination) ||
           destinations>std::numeric_limits<std::size_t>::max()/sizeof(nr_descriptor::UnknownRange)) {
            g_nr_descriptors.BeginUnknownCopy(copy.unknown,nullptr,0,true);return;
        }
        if(!destinations)return;
        copy.ranges.reset(new(std::nothrow) nr_descriptor::UnknownRange[destinations]);
        if(!copy.ranges){g_nr_descriptors.BeginUnknownCopy(copy.unknown,nullptr,0,true);return;}
        unsigned range_count=0;
        for(UINT i=0;i<destinations;++i) {
            const auto size=destination_sizes?destination_sizes[i]:1;
            if(!size)continue;
            const auto first=static_cast<nr_descriptor::Id>(destination[i].ptr);
            if(!first || static_cast<nr_descriptor::Id>(size-1)>
                    (std::numeric_limits<nr_descriptor::Id>::max()-first)/stride) {
                g_nr_descriptors.BeginUnknownCopy(copy.unknown,nullptr,0,true);return;
            }
            copy.ranges[range_count++]={first,first+static_cast<nr_descriptor::Id>(size-1)*stride,
                static_cast<unsigned>(type)};
        }
        if(range_count)g_nr_descriptors.BeginUnknownCopy(copy.unknown,copy.ranges.get(),range_count);
    };
    if(destinations>64 || sources>64 || (destinations&&!destination) || (sources&&!source)) {
        unknown_destinations();return;
    }
    std::uint64_t destination_count=0,source_count=0;
    for(UINT i=0;i<destinations;++i)destination_count+=destination_sizes?destination_sizes[i]:1;
    for(UINT i=0;i<sources;++i)source_count+=source_sizes?source_sizes[i]:1;
    if(destination_count!=source_count || destination_count>NrDescriptorCopy::kMaximum) {
        unknown_destinations();return;
    }
    if(!destination_count)return;
    if(!stride){unknown_destinations();return;}
    copy.writes.reset(new(std::nothrow) nr_descriptor::Write[static_cast<unsigned>(destination_count)]);
    if(!copy.writes){unknown_destinations();return;}
    unsigned destination_range=0,destination_offset=0,source_range=0,source_offset=0;
    for(std::uint64_t n=0;n<destination_count;++n) {
        while(destination_range<destinations && destination_offset==(destination_sizes?destination_sizes[destination_range]:1)) {++destination_range;destination_offset=0;}
        while(source_range<sources && source_offset==(source_sizes?source_sizes[source_range]:1)) {++source_range;source_offset=0;}
        if(destination_range==destinations || source_range==sources){unknown_destinations();return;}
        const auto destination_handle=destination[destination_range].ptr+static_cast<std::uint64_t>(destination_offset++)*stride;
        const auto source_handle=source[source_range].ptr+static_cast<std::uint64_t>(source_offset++)*stride;
        if(destination_handle<destination[destination_range].ptr || source_handle<source[source_range].ptr) {
            unknown_destinations();return;
        }
        copy.writes[copy.count++]=g_nr_descriptors.BeginCopy(destination_handle,source_handle,static_cast<unsigned>(type));
    }
}
bool NrDescriptorFormatScopeMatches(const NrNativeFormatScope &scope,unsigned role,
        ID3D12Device *device,ID3D12Resource *resource) {
    // Device interface aliases are supported. A resource alias is not proof
    // that the resource passed to the later native codec can be unwrapped.
    if(!device || !resource || scope.resources[role]!=resource)return false;
    // Preserve the original exact-interface path even if optional provenance
    // capacity/publication is unavailable. Only aliases need the extra proof.
    if(scope.device==device && scope.resources[role]==resource)return true;
    const auto key=scope.keys[role];
    if(!key.device || NrFormatDeviceIdentity(device)!=key.device)return false;
    const auto life=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(device),NrDescriptorObject::Device,key.device);
    if(!life || life!=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(scope.device),NrDescriptorObject::Device,key.device))return false;
    const auto value=NrDescriptorTrackResource(device,resource);
    const auto expected_semantic=NrFormatResourceIdentity(scope.resources[role]);
    const auto expected=NrDescriptorObjectIdentity(reinterpret_cast<ID3D12Object *>(scope.resources[role]),
        NrDescriptorObject::Resource,key.device,expected_semantic);
    return value && expected && value.id==expected &&
        NrFormatResourceIdentity(resource)==key.resource;
}
bool EnsureNrDescriptorHooks(ID3D12Device *,bool canonical_native=false);
bool EnsureNrDescriptorCommandHooks(ID3D12GraphicsCommandList *);
bool NrDescriptorCommandHooksReady(ID3D12GraphicsCommandList *);
nr_post_boundary::List ResolvePostBoundaryListForObservation(ID3D12GraphicsCommandList *);
// Only the current original API call may lend its cold-verified alias to hooks.
// No owning graphics reference, heap allocation, or address cache survives it.
struct NrPostBoundaryAliasScope {
    NrPostBoundaryAliasScope *parent=nullptr;
    std::uint64_t original=0,epoch=0;
    nr_post_boundary::List identity;
    explicit NrPostBoundaryAliasScope(ID3D12GraphicsCommandList *);
    ~NrPostBoundaryAliasScope();
};
thread_local NrPostBoundaryAliasScope *g_nr_post_boundary_alias_scope=nullptr;
std::mutex g_nr_descriptor_use_mutex;
nr_descriptor::NativeUse g_nr_descriptor_uses[64];
std::uint64_t g_nr_descriptor_next_call=0;
thread_local std::uint64_t g_nr_descriptor_active_call=0;
struct NrDescriptorNativeUseScope {
    std::uint64_t call=0,previous=0;
    bool active=true;
    NrDescriptorNativeUseScope(NrNativeFormatScope &format,ID3D12GraphicsCommandList *list,
                              std::uint64_t generation) {
        previous=g_nr_descriptor_active_call;g_nr_descriptor_active_call=0;
        const auto identity=SnapshotPostBoundaryList(list);
        const auto output=NrDescriptorTrackResource(format.device,format.resources[1]);
        const bool ready=NrDescriptorDeviceCoverage(format.device).ready && identity.known &&
            NrDescriptorCommandHooksReady(reinterpret_cast<ID3D12GraphicsCommandList *>(identity.address));
        std::lock_guard guard(g_nr_descriptor_use_mutex);
        call=++g_nr_descriptor_next_call;
        auto &entry=g_nr_descriptor_uses[call%64];entry={};entry.call=call;
        entry.config=g_config_generation.load(std::memory_order_relaxed);entry.feature=generation;
        entry.carrier=g_carrier_evaluations.load(std::memory_order_relaxed);
        entry.device=output.device;entry.resource=output.id;
        entry.semantic_resource=format.keys[1].resource;
        entry.resource_address=reinterpret_cast<std::uint64_t>(format.resources[1]);
        entry.list=identity.token;entry.reset=identity.reset;entry.list_address=identity.address;
        entry.identity_known=identity.known && static_cast<bool>(output);entry.hooks_ready=ready;
        g_nr_descriptor_active_call=call;
    }
    void Finish(bool success) {
        if(!active)return;
        g_nr_descriptor_active_call=previous;
        std::lock_guard guard(g_nr_descriptor_use_mutex);
        auto &entry=g_nr_descriptor_uses[call%64];
        if(entry.call==call)entry.Finish(success,g_nr_descriptors);
        active=false;
    }
    ~NrDescriptorNativeUseScope(){Finish(false);}
};
void NrObserveNativeClearRequest(ID3D12GraphicsCommandList*,ID3D12Resource*,
        const nr_descriptor::Pair&,const nr_descriptor::Pair&,UINT,const D3D12_RECT*);
void NrDescriptorObserveNativeClear(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,
        const nr_descriptor::Pair &pair,const nr_descriptor::Pair &after,UINT count,const D3D12_RECT *rects) {
    NrObserveNativeClearRequest(list,resource,pair,after,count,rects);
    const auto call=g_nr_descriptor_active_call;
    if(!call)return;
    const auto identity=SnapshotPostBoundaryList(list);
    const auto output=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(resource));
    std::lock_guard guard(g_nr_descriptor_use_mutex);
    auto &entry=g_nr_descriptor_uses[call%64];
    if(entry.call!=call || entry.finished)return;
    if(!identity.known || entry.list_address!=identity.address || entry.list!=identity.token || entry.reset!=identity.reset) {
        ++entry.other_lists;return;
    }
    if(entry.resource_address!=reinterpret_cast<std::uint64_t>(resource) &&
       (!output || entry.resource!=output.id || entry.device!=output.device)){++entry.other_resources;return;}
    entry.Observe(pair);
}
void NrDescriptorObserveSubmission(const nr_post_boundary::List &list,std::uint64_t queue,
        std::uint64_t submission,unsigned index,unsigned total) {
    std::lock_guard guard(g_nr_descriptor_use_mutex);
    for(auto &entry:g_nr_descriptor_uses)if(entry.call && list.known && entry.identity_known &&
        entry.list_address==list.address && entry.list==list.token && entry.reset==list.reset)
        entry.Submit(queue,submission,index,total,g_nr_descriptors);
}
bool NrDescriptorHasPendingUses() {
    std::lock_guard guard(g_nr_descriptor_use_mutex);
    for(const auto &entry:g_nr_descriptor_uses)
        if(entry.count && entry.finished && entry.native_success && !entry.submission_seen)return true;
    return false;
}
void NrDescriptorExecuteReturned(std::uint64_t submission) {
    if(!submission)return;
    std::lock_guard guard(g_nr_descriptor_use_mutex);
    for(auto &entry:g_nr_descriptor_uses)if(entry.submission_seen && entry.submission==submission)entry.execute_returned=true;
}
nr_descriptor::NativeUse SnapshotNrDescriptorUse(std::uint64_t call=0) {
    std::lock_guard guard(g_nr_descriptor_use_mutex);
    if(!call)call=g_nr_descriptor_next_call;
    const auto &entry=g_nr_descriptor_uses[call%64];
    return entry.call==call?entry:nr_descriptor::NativeUse{};
}
std::uint64_t NrDescriptorResourceToken(std::uint64_t address) {
    return g_nr_descriptors.ResourceAt(address).id;
}
