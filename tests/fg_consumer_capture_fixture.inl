// Real on-disk capture verification, not a handoff mock. The production worker
// must export after both the actual queue fence and command recording retirement.
void ValidateFgCapture(const wchar_t* output,unsigned passes,unsigned scale,unsigned hud_mode){
    const auto deadline=GetTickCount64()+10000;
    while((nr_fg_capture::workers.load()||nr_fg_capture::status.load()==1)&&GetTickCount64()<deadline){nr_fg_capture::Collect();Sleep(2);}
    Check(nr_fg_capture::status.load()==2&&!nr_fg_capture::workers.load()&&!nr_fg_capture::remaining.load(),"real pre-FG capture export completed every layer");
    wchar_t documents[768]{};Hr(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,documents),"FG capture documents");
    const auto request=nr_fg_capture::consumed;Check(request!=0,"real FG capture consumed request");
    unsigned long long common_frame=0;std::vector<unsigned char> source_plane,composite_plane,previous_neural;
    for(unsigned layer=0;layer<passes;++layer){wchar_t path[1400]{},destination[1400]{},metadata[1400]{};
        swprintf_s(path,L"%ls\\DLSS5-FG-Capture\\%lu-%llu\\layer-%u.nrf",documents,GetCurrentProcessId(),request,layer+1);
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"actual pre-FG NRF exists");
        nr_replay::Header header{};DWORD got=0;Check(ReadFile(file,&header,sizeof(header),&got,nullptr)&&got==sizeof(header),"actual pre-FG NRF header read");
        LARGE_INTEGER length{};Check(GetFileSizeEx(file,&length)&&static_cast<unsigned long long>(length.QuadPart)==sizeof(header)+header.payload_bytes,"actual pre-FG full payload length");
        Check(nr_replay::ValidHeader(header)&&header.completion==unsigned(nr_replay::Completion::Complete),"actual pre-FG NRF ABI and complete");
        Check(header.parameter_count>0&&header.codec_constant_count>0&&header.since_reset>g_config.warmup_after_create,"actual warm parameters and final composition constants");
        const NrContext* context=nullptr;for(const auto& c:g_contexts)if(c.instance_id==header.instance)context=&c;
        Check(context&&context->nr_handle&&context->layer_index==layer&&header.mode==context->mode,"captured independent production layer instance");
        Check(header.source_width==Io::width&&header.source_height==Io::height&&header.work_width==context->width&&header.work_height==context->height,"actual source and requested layer capture dimensions");
        if(!layer)common_frame=header.frame;else Check(header.frame==common_frame,"all layers captured one source frame");
        std::vector<unsigned char> payload(static_cast<std::size_t>(header.payload_bytes));
        Check(ReadFile(file,payload.data(),static_cast<DWORD>(payload.size()),&got,nullptr)&&got==payload.size(),"actual FG GPU payload read");CloseHandle(file);
        auto plane=[&](nr_replay::Role role){const auto& p=header.planes[unsigned(role)];Check(p.bytes>0,"complete actual pre-FG capture role");
            return std::vector<unsigned char>(payload.begin()+p.offset,payload.begin()+p.offset+p.bytes);};
        const auto input=plane(nr_replay::Role::Proxy),neural=plane(nr_replay::Role::Neural),source=plane(nr_replay::Role::Source),composite=plane(nr_replay::Role::Composite);
        Check(input.size()==neural.size()&&input!=neural,"actual captured signed NR model result differs from input");
        if(layer){Check(source==source_plane&&composite==composite_plane,"all layers share exact source and final composition");
            if(scale==100)Check(input==previous_neural,"same-size next NR input is previous actual model output");}
        else {source_plane=source;composite_plane=composite;}previous_neural=neural;
        plane(nr_replay::Role::Depth);plane(nr_replay::Role::Motion);
        swprintf_s(destination,L"%ls\\fg-capture-p%u-s%u-hud%u-layer%u.nrf",output,passes,scale,hud_mode,layer+1);Check(CopyFileW(path,destination,TRUE)!=FALSE,"copy observed FG capture evidence");
        swprintf_s(metadata,L"%ls\\DLSS5-FG-Capture\\%lu-%llu\\layer-%u.json",documents,GetCurrentProcessId(),request,layer+1);
        swprintf_s(destination,L"%ls\\fg-capture-p%u-s%u-hud%u-layer%u.json",output,passes,scale,hud_mode,layer+1);Check(CopyFileW(metadata,destination,TRUE)!=FALSE,"copy actual pre-FG role and source metadata");
        std::printf("actual-FG-capture request=%llu sourceFrame=%llu layer=%u instance=%llu work=%ux%u parameters=%u bytes=%llu\n",request,header.frame,layer+1,header.instance,header.work_width,header.work_height,header.parameter_count,header.payload_bytes);
    }
    wchar_t manifest[1400]{},destination[1400]{};
    swprintf_s(manifest,L"%ls\\DLSS5-FG-Capture\\%lu-%llu\\capture.json",documents,GetCurrentProcessId(),request);
    HANDLE file=CreateFileW(manifest,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"actual whole-request FG completion manifest exists");
    char text[2048]{};DWORD got=0;Check(ReadFile(file,text,sizeof(text)-1,&got,nullptr)&&got>0&&got<sizeof(text)-1,"actual whole-request FG manifest read");CloseHandle(file);
    Check(std::strstr(text,"\"complete\":true")!=nullptr,"all-layer capture request completed on disk");
    swprintf_s(destination,L"%ls\\fg-capture-p%u-s%u-hud%u-complete.json",output,passes,scale,hud_mode);Check(CopyFileW(manifest,destination,TRUE)!=FALSE,"copy actual whole-request FG manifest");
}
