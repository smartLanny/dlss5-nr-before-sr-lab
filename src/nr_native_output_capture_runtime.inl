// Included after nr_source_frame_runtime; explicit deep capture only. Source
// state comes from its complete, exact-recording legacy-state table. The
// diagnostic post-boundary event ring is never used as barrier permission.
void PrimeNativeOutputCapture(ID3D12GraphicsCommandList* list) {
    const unsigned status=StageCaptureStatus();
    if(status==1||status==2)(void)EnsurePostBoundaryNativeHooks(list);
}
NrNativeOutputCapture* ArmNativeOutputCapture(ID3D12GraphicsCommandList* list,
        const NgxParams* params,const FeatureRecord& feature,NrSlot* slot) {
    using namespace nr_native_capture;
    if(!slot||!slot->stage_capture.pending||slot->stage_capture.info.mode!=2||
       slot->stage_capture.native_output.header.reason!=Reason::NotRequested)return nullptr;
    auto& capture=slot->stage_capture.native_output;auto& h=capture.header;
    const auto& stages=slot->stage_capture.info;
    h.request=stages.request_id;h.instance=stages.identity.instance_id;h.frame=stages.identity.frame_id;
    h.feature=feature.generation;h.config=stages.identity.config_generation;h.evaluation=stages.identity.evaluation;
    h.create_flags=static_cast<unsigned>(feature.create_flags);
    const auto exposure=ReadColorContract(params);h.pre_exposure=exposure.pre_exposure;h.exposure_scale=exposure.exposure_scale;
    std::snprintf(h.source_commit,sizeof(h.source_commit),"%s",kSourceCommit);
    std::snprintf(h.build,sizeof(h.build),"%s",kBuildIdentity);
    const auto source=nr_source_runtime::CurrentSource();
    h.source_stream=source.stream;h.source_epoch=source.epoch;h.source_sequence=source.sequence;
    h.reason=Reason::IdentityUnknown;
    const auto recording=SnapshotPostBoundaryList(list);
    if(!source||!h.request||!h.instance||!h.frame||feature.feature_id!=1||
       feature.generation!=stages.feature_generation||!recording.known||
       !ReadLifecycleToken(list,h.recording))return &capture;
    h.reset=recording.reset;
    capture.coverage=EnsurePostBoundaryNativeHooks(list);
    using namespace nr_post_boundary;
    if((capture.coverage&(HookMissing|InterfaceUnknown|IdentityUnknown))||
       !(capture.coverage&Legacy)||!(capture.coverage&(Enhanced|EnhancedAbsent))) {
        h.reason=Reason::HooksIncomplete;return &capture;
    }
    ID3D12Resource* output=nullptr;
    if(!GetResource(params,"Output",&output)||!output){h.reason=Reason::OutputMissing;return &capture;}
    const auto desc=ID3D12Resource_GetDesc(output);
    h.storage_format=static_cast<unsigned>(desc.Format);h.view_format=h.storage_format;
    h.bytes_per_pixel=BytesPerPixel(h.storage_format);
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||desc.MipLevels!=1||
       desc.SampleDesc.Count!=1||desc.Width>16384||desc.Height>16384||!h.bytes_per_pixel) {
        h.reason=Reason::UnsupportedStorage;return &capture;
    }
    h.resource_width=static_cast<unsigned>(desc.Width);h.resource_height=desc.Height;
    GetUInt(params,"DLSS.Output.Subrect.Base.X",&h.x);GetUInt(params,"DLSS.Output.Subrect.Base.Y",&h.y);
    if(!ResolveOutputExtentFromEvaluate(params,feature,output,h.x,h.y,stages.source_width,stages.source_height,h.width,h.height)||
       !h.width||!h.height||h.x>desc.Width||h.y>desc.Height||h.width>desc.Width-h.x||h.height>desc.Height-h.y) {
        h.reason=Reason::InvalidRect;return &capture;
    }
    ID3D12Device* device=nullptr;ID3D12Device* owner=nullptr;
    const bool same=SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list,IID_ID3D12Device,reinterpret_cast<void**>(&device)))&&device&&
        SUCCEEDED(ID3D12Resource_GetDevice(output,IID_ID3D12Device,reinterpret_cast<void**>(&owner)))&&owner&&IsSameRrDepthDevice(device,owner);
    ReleaseCom(owner);
    if(!same){ReleaseCom(device);h.reason=Reason::IdentityUnknown;return &capture;}
    auto footprint_desc=desc;footprint_desc.Width=h.width;footprint_desc.Height=h.height;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    ID3D12Device_GetCopyableFootprints(device,&footprint_desc,0,1,0,&footprint,nullptr,nullptr,&capture.readback_bytes);
    capture.row_pitch=footprint.Footprint.RowPitch;h.row_bytes=h.width*h.bytes_per_pixel;
    h.payload_bytes=std::uint64_t(h.row_bytes)*h.height;
    if(footprint.Offset||capture.row_pitch<h.row_bytes||!capture.readback_bytes||capture.readback_bytes>kReadbackLimit) {
        ReleaseCom(device);h.reason=Reason::Budget;return &capture;
    }
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=capture.readback_bytes;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    const auto hr=ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&capture.readback));ReleaseCom(device);
    if(FAILED(hr)||!capture.readback){h.reason=Reason::Allocation;return &capture;}
    capture.source=output;ID3D12Resource_AddRef(output);h.status=Status::Armed;h.reason=Reason::None;
    return &capture;
}
void FinishNativeOutputCapture(ID3D12GraphicsCommandList* list,const NgxParams* params,
        NgxResult result,NrNativeOutputCapture* capture) {
    using namespace nr_native_capture;
    if(!capture)return;auto& h=capture->header;h.native_result=result;
    if(h.status!=Status::Armed)return;
    h.status=Status::Missing;
    if(result!=kNgxSuccess){h.reason=Reason::NativeFailed;return;}
    unsigned long long token=0;const auto recording=SnapshotPostBoundaryList(list);
    if(!ReadLifecycleToken(list,token)||token!=h.recording||!recording.known||recording.reset!=h.reset) {
        h.reason=Reason::RecordingChanged;return;
    }
    const auto source=nr_source_runtime::CurrentSource();ID3D12Resource* output=nullptr;
    if(source.stream!=h.source_stream||source.epoch!=h.source_epoch||source.sequence!=h.source_sequence||
       !GetResource(params,"Output",&output)||output!=capture->source){h.reason=Reason::SourceChanged;return;}
    {
        nr_source_runtime::Lock lock;
        if(!nr_source_runtime::StateOf(nr_source_runtime::RecordingOf(list),nr_source_runtime::ResourceOf(output),h.state)) {
            h.reason=Reason::StateUnknown;return;
        }
    }
    NativeCommandStateScope restore(list);
    if(!restore.active){h.reason=Reason::CommandStateUnknown;return;}
    // Copy is predicable. Disable only inside a proven restorable envelope.
    ID3D12GraphicsCommandList_SetPredication(restore.command_list,nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    auto state=static_cast<D3D12_RESOURCE_STATES>(h.state);
    Transition(restore.command_list,output,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=output;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=capture->readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint={static_cast<DXGI_FORMAT>(h.storage_format),h.width,h.height,1,capture->row_pitch};
    const D3D12_BOX box{h.x,h.y,0,h.x+h.width,h.y+h.height,1};
    ID3D12GraphicsCommandList_CopyTextureRegion(restore.command_list,&to,0,0,0,&from,&box);
    Transition(restore.command_list,output,state,static_cast<D3D12_RESOURCE_STATES>(h.state));
    restore.Restore();h.status=Status::Recorded;h.reason=Reason::None;
}
