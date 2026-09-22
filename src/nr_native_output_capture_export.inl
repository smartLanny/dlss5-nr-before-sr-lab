// Included by the stage exporter after WriteStageCaptureBytes. No GPU work.
struct StageCaptureExportReceipt {
    unsigned long long request=0;
    bool stages_complete=false;
    nr_native_capture::Status native_status=nr_native_capture::Status::Missing;
    nr_native_capture::Reason native_reason=nr_native_capture::Reason::NotRequested;
    wchar_t directory[MAX_PATH]{};
};
SRWLOCK g_stage_export_receipt_lock=SRWLOCK_INIT;
StageCaptureExportReceipt g_stage_export_receipt{};
bool ReadStageCaptureExportReceipt(unsigned long long request,StageCaptureExportReceipt& out) {
    AcquireSRWLockShared(&g_stage_export_receipt_lock);out=g_stage_export_receipt;
    ReleaseSRWLockShared(&g_stage_export_receipt_lock);return request&&out.request==request;
}
bool ExportNativeOutputCapture(const wchar_t* directory,NrNativeOutputCapture& capture) {
    using namespace nr_native_capture;
    auto& h=capture.header;
    bool pixels=false;
    if(ValidPixels(h)&&capture.readback) {
        void* mapped=nullptr;const D3D12_RANGE range={0,static_cast<SIZE_T>(capture.readback_bytes)};
        if(SUCCEEDED(ID3D12Resource_Map(capture.readback,0,&range,&mapped))&&mapped) {
            wchar_t path[MAX_PATH]{};
            HANDLE file=swprintf_s(path,L"%ls\\native-output.nro",directory)>=0?
                CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
            if(file!=INVALID_HANDLE_VALUE) {
                pixels=WriteStageCaptureBytes(file,&h,sizeof(h));
                for(unsigned y=0;pixels&&y<h.height;++y)
                    pixels=WriteStageCaptureBytes(file,static_cast<const unsigned char*>(mapped)+std::size_t(y)*capture.row_pitch,h.row_bytes);
                CloseHandle(file);
            }
            const D3D12_RANGE written={0,0};ID3D12Resource_Unmap(capture.readback,0,&written);
        }
        if(!pixels){h.status=Status::Missing;h.reason=Reason::ExportFailed;}
    } else if(h.status==Status::Complete){h.status=Status::Missing;h.reason=Reason::CompletionUnknown;}
    wchar_t path[MAX_PATH]{};
    HANDLE file=swprintf_s(path,L"%ls\\native-output.json",directory)>=0?
        CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr):INVALID_HANDLE_VALUE;
    if(file==INVALID_HANDLE_VALUE)return false;
    char text[4096]{};
    const int n=std::snprintf(text,sizeof(text),
        "{\n\"schema\":\"nr-native-output-sidecar-v1\",\"present\":%s,\"reason\":\"%s\","
        "\"file\":\"%s\",\"request\":%llu,\"instance\":%llu,\"frame\":%llu,\"feature_generation\":%llu,"
        "\"source\":[%llu,%llu,%llu],\"recording\":%llu,\"reset\":%llu,\"queue\":%llu,\"fence\":%llu,"
        "\"config\":%u,\"evaluation\":%u,\"native_result\":%u,\"extent\":[%u,%u],\"origin\":[%u,%u],"
        "\"storage_format\":%u,\"view_format\":%u,\"state_before_copy\":%u,\"native_create_flags\":%u,"
        "\"bytes\":%llu,\"source_commit\":\"%s\",\"build\":\"%s\","
        "\"colour_domain\":\"native storage; transfer and primaries unverified; decode by storage_format only\","
        "\"pre_exposure\":%.9g,\"exposure_scale\":%.9g,"
        "\"completion\":\"%s\","
        "\"scope\":\"explicit deep capture after one native SR call; not a screenshot, performance sample or temporal convergence claim\"\n}\n",
        pixels?"true":"false",ReasonName(h.reason),pixels?"native-output.nro":"",h.request,h.instance,h.frame,h.feature,
        h.source_stream,h.source_epoch,h.source_sequence,h.recording,h.reset,h.queue,h.fence,h.config,h.evaluation,
        h.native_result,h.width,h.height,h.x,h.y,h.storage_format,h.view_format,h.state,h.create_flags,
        pixels?h.payload_bytes:0,h.source_commit,h.build,double(h.pre_exposure),double(h.exposure_scale),
        pixels?"same recording as P/N/E; actual final fence completed and recording retired":"O completion not available");
    const bool ok=n>0&&std::size_t(n)<sizeof(text)&&WriteStageCaptureBytes(file,text,static_cast<DWORD>(n));
    CloseHandle(file);return ok;
}
void PublishStageCaptureExportReceipt(unsigned long long request,const wchar_t* directory,bool ok,
                                      const NrNativeOutputCapture& native) {
    StageCaptureExportReceipt receipt{};receipt.request=request;receipt.stages_complete=ok;
    receipt.native_status=native.header.status;receipt.native_reason=native.header.reason;
    if(directory)wcsncpy_s(receipt.directory,directory,_TRUNCATE);
    AcquireSRWLockExclusive(&g_stage_export_receipt_lock);g_stage_export_receipt=receipt;
    ReleaseSRWLockExclusive(&g_stage_export_receipt_lock);
}
