// Test-only observation around the untouched original NGX callback. No runtime
// admission, state evidence, model setting or temporal result is fabricated.
namespace pre_amd_observe {
using queue_test::Check;using queue_test::Hr;
EvaluateFeatureFn original=nullptr;
unsigned long long frame_before=0,seen=0,continued=0,recorded_at_capture=0;
ID3D12Resource* input_readback=nullptr;
D3D12_PLACED_SUBRESOURCE_FOOTPRINT input_footprint{};
UINT64 input_bytes=0;
unsigned long long source_stream=0,source_epoch=0,source_sequence=0,parent_frame=0;
nr::final_residual::TemporalStats last{};
NgxResult Evaluate(ID3D12GraphicsCommandList* list,const NgxHandle* handle,const NgxParams* params,void* callback){
    const auto recorded=nr_temporal_status::recorded.load();
    if(recorded>frame_before){
        Check(recorded==frame_before+1,"exactly one temporal recording precedes this native SR");
        Check(g_config.temporal_backend==2&&g_config.temporal_scope==0&&
            nr_processing_runtime::Effective()==nr_processing::Start::Before,"actual pre-SR AMD whole-chain route");
        ID3D12Resource* color=nullptr;Check(GetResource(params,"Color",&color)&&color,"actual substituted native Color");
        NrContext* owner=nullptr;NrSlot* input=nullptr;
        for(auto& context:g_contexts)if(context.sr_handle==handle&&context.mode==2&&context.temporal&&context.temporal_key==2)
            for(auto& slot:context.slots)if(slot.result==color){Check(!owner,"unique current temporal input owner");owner=&context;input=&slot;}
        Check(owner&&input&&input->source!=color,"native SR consumes the actual temporal candidate, with a separate original");
        unsigned long long token=0;Check(ReadLifecycleToken(list,token)&&token==owner->temporal_token,"same recording reaches temporal then SR");
        last=owner->temporal->stats();Check(!last.faulted&&last.pending_token&&last.recorded,"actual AMD recording pending final consumer");
        ++seen;if(last.recorded>last.seeded)++continued;
        if(input->stage_capture.pending){
            Check(!input_readback,"one deep capture input snapshot");
            const auto source=nr_source_runtime::CurrentSource();source_stream=source.stream;source_epoch=source.epoch;source_sequence=source.sequence;
            parent_frame=input->stage_capture.info.identity.frame_id;recorded_at_capture=recorded;
            const auto desc=ID3D12Resource_GetDesc(color);
            Check(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Width==320&&desc.Height==192&&
                  input->result_state==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"actual pre-SR FP16 input storage/state");
            ID3D12Device* device=nullptr;Hr(ID3D12Resource_GetDevice(color,IID_ID3D12Device,(void**)&device),"snapshot device");
            ID3D12Device_GetCopyableFootprints(device,&desc,0,1,0,&input_footprint,nullptr,nullptr,&input_bytes);
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=input_bytes;
            buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
            Hr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,(void**)&input_readback),"post-temporal input readback");ReleaseCom(device);
            NativeCommandStateScope restore(list);Check(restore.active,"read-only observer preserves native command state");
            ID3D12GraphicsCommandList_SetPredication(restore.command_list,nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
            auto state=input->result_state;Transition(restore.command_list,color,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=input_readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=input_footprint;
            ID3D12GraphicsCommandList_CopyTextureRegion(restore.command_list,&to,0,0,0,&from,nullptr);
            Transition(restore.command_list,color,state,input->result_state);restore.Restore();
        }
    }
    return original(list,handle,params,callback);
}
void Finish(const wchar_t* directory,const StageCaptureExportReceipt& receipt){
    Check(seen>=2&&continued&&nr_temporal_status::recorded.load()==seen&&nr_temporal_status::submitted.load()==seen,
          "actual AMD history continues and every recorded result reaches Execute plus final Signal");
    Check(input_readback&&recorded_at_capture==seen&&parent_frame==64&&!g_lifecycle_tracking_uncertain.load(),
          "same 64th NR frame contains observed temporal input and O; no lifecycle quarantine");
    wchar_t path[MAX_PATH]{};swprintf_s(path,L"%ls\\post-temporal-input.rgba16",directory);
    FILE* file=_wfopen(path,L"wb");Check(file!=nullptr,"store actual post-temporal SR input");
    void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(input_bytes)};
    Hr(ID3D12Resource_Map(input_readback,0,&range,&mapped),"map only after actual final fence and recording Reset");
    for(unsigned y=0;y<192;++y)Check(fwrite(static_cast<unsigned char*>(mapped)+input_footprint.Offset+y*input_footprint.Footprint.RowPitch,8,320,file)==320,"input row");
    fclose(file);D3D12_RANGE none{};ID3D12Resource_Unmap(input_readback,0,&none);ReleaseCom(input_readback);
    swprintf_s(path,L"%ls\\pre-amd-observation.json",directory);file=_wfopen(path,L"wb");Check(file!=nullptr,"AMD receipt");
    std::fprintf(file,"{\"schema\":\"pre-amd-native-output-v1\",\"backend\":2,\"scope\":0,\"native_calls_after_temporal\":%llu,\"recorded\":%llu,\"submitted\":%llu,\"seeded\":%llu,\"continuation_observations\":%llu,\"last_weight\":%.9g,\"source\":[%llu,%llu,%llu],\"parent_nr_frame\":%llu,\"capture_request\":%llu,\"input_extent\":[320,192],\"input_format\":10,\"input_file\":\"post-temporal-input.rgba16\",\"E_is_pre_temporal\":true,\"static_artificial_scene\":true,\"game_verified\":false}\n",
        seen,nr_temporal_status::recorded.load(),nr_temporal_status::submitted.load(),last.seeded,continued,double(last.weight),
        source_stream,source_epoch,source_sequence,parent_frame,receipt.request);fclose(file);
    std::printf("PRE_AMD recorded=%llu submitted=%llu continued=%llu seeded=%llu weight=%.9g; real NR -> Combine -> AMD -> original Feature1\n",
        seen,nr_temporal_status::submitted.load(),continued,last.seeded,double(last.weight));
}
}
