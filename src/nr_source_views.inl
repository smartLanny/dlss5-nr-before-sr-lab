// RTV identity from actual ReShade view creation/binding. This is independent
// of the optional sampled post-SR descriptor diagnostic window.
struct SourceRtv {reshade::api::device* owner=nullptr;Id handle=0;Resource resource{};bool whole=false;};
SourceRtv source_rtvs[512]{};
bool WholeSourceResource(ID3D12Resource* resource){
    if(!resource)return false;const auto desc=ID3D12Resource_GetDesc(resource);
    return desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc.MipLevels==1&&desc.DepthOrArraySize==1&&desc.SampleDesc.Count==1;
}
SourceRtv* FindSourceRtv(Id handle,Id device){
    for(auto& view:source_rtvs)if(view.handle==handle){
        if(!view.resource.lifetime){
            // The init event may occur while F6 is off. Its scalar address and
            // view description survive, but COM identity/shape are resolved
            // only at a real enabled use. Destruction events erase the entry.
            auto* native=reinterpret_cast<ID3D12Resource*>(view.resource.address);
            const auto resource=ResourceOf(native);if(!resource)continue;
            view.resource=resource;view.whole=view.whole&&WholeSourceResource(native);
        }
        if(view.resource.device==device)return &view;
    }
    return nullptr;
}
void RememberSourceRtv(reshade::api::device* owner,Id handle,Resource resource,bool whole){
    if(!owner||!handle)return;SourceRtv* selected=nullptr;
    for(auto& view:source_rtvs){if(view.owner==owner&&view.handle==handle){selected=&view;break;}
        if(!view.handle&&!selected)selected=&view;}
    if(selected)*selected={owner,handle,resource,whole};
}
void ForgetSourceRtvResource(Id address){for(auto& view:source_rtvs)if(view.resource.address==address)view={};}
void OnSourceInitView(reshade::api::device* device,reshade::api::resource resource,reshade::api::resource_usage usage,
    const reshade::api::resource_view_desc& desc,reshade::api::resource_view view){
    if(usage!=reshade::api::resource_usage::render_target||!device||device->get_api()!=reshade::api::device_api::d3d12)return;
    Lock lock;
    // A native null RTV description arrives as unknown. It is only a whole
    // candidate here; enabled use must still verify the actual single-surface
    // resource shape before a clear can replace prior pixel provenance.
    const bool whole=(desc.type==reshade::api::resource_view_type::unknown||desc.type==reshade::api::resource_view_type::texture_2d)&&
        !desc.texture.first_level&&!desc.texture.first_layer;
    RememberSourceRtv(device,view.handle,{resource.handle,0,0,0},whole);
}
void OnSourceDestroyView(reshade::api::device* device,reshade::api::resource_view view){
    Lock lock;for(auto& old:source_rtvs)if(old.owner==device&&old.handle==view.handle)old={};
}
void OnClearRtv(ID3D12GraphicsCommandList* list,D3D12_CPU_DESCRIPTOR_HANDLE handle,bool full){
    if(!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return;
    Lock lock;const auto recording=RecordingOf(list);if(!recording)return;
    const auto* view=FindSourceRtv(handle.ptr,recording.device);if(!view)return;
    if(full&&view->whole)lineage.Clear(recording,view->resource);else lineage.Invalidate(recording,view->resource);
    if(lineage.PendingList(recording))source_receipts_pending.store(true,std::memory_order_release);
}
unsigned long long ObservedRtvResource(reshade::api::command_list* owner,unsigned long long handle){
    if(!owner||!handle||!nr_processing_runtime::work_enabled.load(std::memory_order_relaxed))return 0;
    Lock lock;const auto recording=RecordingOf(reinterpret_cast<ID3D12GraphicsCommandList*>(owner->get_native()));
    if(!recording)return 0;const auto* view=FindSourceRtv(handle,recording.device);return view?view->resource.address:0;
}
