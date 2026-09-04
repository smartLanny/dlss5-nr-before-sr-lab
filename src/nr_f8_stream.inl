// Versioned records and bounded streaming IO. Included inside nr_f8 after Frame/Group.
struct JsonBuffer {
    char data[8192] = {};
    std::size_t used = 0;
    bool ok = true;
    void Append(const char *format, ...) {
        if (!ok) return;
        va_list args; va_start(args,format);
        const int n=std::vsnprintf(data+used,sizeof(data)-used,format,args); va_end(args);
        if(n<0 || static_cast<std::size_t>(n)>=sizeof(data)-used){ok=false;return;}
        used+=static_cast<std::size_t>(n);
    }
    void Float(const char *key,float value) {
        Append(",\"%s\":",key);
        if(std::isfinite(value))Append("%.9g",static_cast<double>(value));else Append("null");
    }
};
void ConfigJson(JsonBuffer &b,const char *key,const ChainConfig &c) {
    b.Append(",\"%s\":{\"enabled\":%d,\"mode\":%d,\"style\":%u,\"autoMask\":%d,\"uiCorrection\":%d,\"workMode\":%d,\"jitterMode\":%d,\"motionRequested\":%d,\"identityUseOriginal\":%d,\"proxyCurve\":%u,\"unverifiedHdrColourRequested\":%d",
        key,c.enabled,c.mode,c.style,c.auto_mask,c.ui_correction,c.work_mode,static_cast<int>(c.jitter_mode),c.motion_stability,c.identity_use_original,c.proxy_curve,c.allow_unverified_hdr_color);
    b.Append(",\"suppressResetFrame\":%d,\"preserveLighting\":%d,\"experimentalPeakShoulder\":%d,\"warmupAfterCreate\":%u,\"retryOriginalOnSrFailure\":%d,\"depthMode\":%d,\"r8OutputEncoding\":%d,\"perfEpoch\":%d",
        c.suppress_reset_frame,c.preserve_lighting,c.experimental_peak_shoulder,c.warmup_after_create,c.retry_original_on_sr_failure,c.depth_mode,c.r8_output_encoding,c.perf_epoch);
    b.Float("transfer",c.transfer_strength);b.Float("postTransfer",c.post_transfer_strength);
    b.Float("colour",c.color_strength);b.Float("modelIntensity",c.intensity);
    b.Float("skin",c.skin_structure);b.Float("localTone",c.local_tone);b.Float("localStructure",c.local_structure);
    b.Float("proxyScale",c.proxy_scale);b.Float("customWorkScale",c.custom_work_scale);
    b.Float("maxLumaRatio",c.max_luma_ratio);b.Float("lightingLock",c.lighting_lock);
    b.Float("edgeGuard",c.edge_guard);b.Float("detailStability",c.detail_stability);b.Append("}");
}
JsonBuffer FrameJson(const NrStageCaptureInfo &n,const NrCaptureParams &p,
                     const ChainConfig &requested,const ChainConfig &actual) {
    JsonBuffer b;
    b.Append("{\"schema\":4,\"mode\":%d,\"codecFlags\":%u,\"sourceFormat\":%u,\"resultFormat\":%u,\"sourceBaseX\":%u,\"sourceBaseY\":%u,\"destinationBaseX\":%u,\"destinationBaseY\":%u,\"regionWidth\":%u,\"regionHeight\":%u,\"sinceReset\":%u,\"evaluation\":%u,\"warmedUp\":%d,\"resetKnown\":%d,\"reset\":%d,\"gameJitterValid\":%d,\"nrJitterForwarded\":%d,\"gameMvValid\":%d,\"depthWidth\":%u,\"depthHeight\":%u,\"mvWidth\":%u,\"mvHeight\":%u,\"hasPreExposure\":%d,\"hasExposureScale\":%d,\"motionAvailable\":false",
        n.mode,n.codec_flags,n.source_format,n.result_format,n.source_base_x,n.source_base_y,n.destination_base_x,n.destination_base_y,n.region_width,n.region_height,n.identity.since_reset,n.identity.evaluation,n.identity.warmed_up,p.reset_known,p.reset,p.game_jitter_valid,p.nr_jitter_forwarded,p.game_mv_valid,p.depth_width,p.depth_height,p.mv_width,p.mv_height,n.has_pre_exposure,n.has_exposure_scale);
    b.Float("gameJitterX",p.game_jitter_x);b.Float("gameJitterY",p.game_jitter_y);
    b.Float("nrJitterX",p.nr_jitter_x);b.Float("nrJitterY",p.nr_jitter_y);
    b.Float("gameMvX",p.game_mv_x);b.Float("gameMvY",p.game_mv_y);
    b.Float("nrMvX",p.nr_mv_x);b.Float("nrMvY",p.nr_mv_y);
    b.Float("preExposure",n.pre_exposure);b.Float("exposureScale",n.exposure_scale);
    b.Float("gpuRawExposure",n.gpu_raw_exposure);b.Float("gpuResolvedExposure",n.gpu_resolved_exposure);
    b.Float("gpuSourceToCanonical",n.gpu_source_to_canonical);b.Float("gpuPreExposure",n.gpu_pre_exposure);
    b.Float("transferEffective",n.transfer_strength);b.Float("colourEffective",n.color_strength);
    b.Float("maxLumaRatio",n.max_luma_ratio);b.Float("proxyScale",n.proxy_scale);
    b.Float("workLinearScale",n.work_linear_scale);b.Float("lightingLock",n.lighting_lock);
    b.Float("edgeGuard",n.edge_guard);b.Float("frameConfidence",n.frame_confidence);
    b.Float("detailStability",n.detail_stability);
    b.Append(",\"appliedTuning\":{\"style\":%u,\"autoMask\":%d,\"uiCorrection\":%d",n.identity.tuning.style,n.identity.tuning.auto_mask,n.identity.tuning.ui_correction);
    b.Float("intensity",n.identity.tuning.intensity);b.Float("skin",n.identity.tuning.skin_structure);
    b.Float("localTone",n.identity.tuning.local_tone);b.Float("localStructure",n.identity.tuning.local_structure);b.Append("}");
    ConfigJson(b,"requested",requested);ConfigJson(b,"actual",actual);b.Append("}");return b;
}
bool WriteBytes(HANDLE h,const void *data,UINT64 size) {
    const auto *p=static_cast<const unsigned char *>(data);
    while(size) {
        const DWORD step=static_cast<DWORD>(std::min<UINT64>(size,65536));DWORD n=0;
        if(!WriteFile(h,p,step,&n,nullptr) || n!=step)return false;
        p+=step;size-=step;
    }
    return true;
}
bool U32(HANDLE h,unsigned v) { unsigned char b[4];for(unsigned i=0;i<4;++i)b[i]=static_cast<unsigned char>(v>>(i*8));return WriteBytes(h,b,4); }
bool U64(HANDLE h,unsigned long long v) { return U32(h,static_cast<unsigned>(v)) && U32(h,static_cast<unsigned>(v>>32)); }
bool WriteRecord(HANDLE h,FrameTicket ticket,const NrStageCaptureInfo &n,const NrCaptureParams &p,
                 const ChainConfig &requested,const ChainConfig &actual,const void *pixels,UINT64 bytes,bool valid) {
    const JsonBuffer json=FrameJson(n,p,requested,actual);
    const UINT64 expected=static_cast<UINT64>(n.width)*n.height*80u;
    if(!json.ok || (valid && (!pixels || bytes!=expected || bytes>40ull*1024*1024)))return false;
    if(!valid)bytes=0;
    const bool ok=U32(h,ticket.index) && U32(h,valid?1:0) && U64(h,ticket.session) && U64(h,n.identity.frame_id) &&
        U64(h,n.identity.instance_id) && U64(h,n.feature_generation) && U32(h,n.identity.config_generation) &&
        U32(h,n.source_width) && U32(h,n.source_height) && U32(h,n.model_width) && U32(h,n.model_height) &&
        U32(h,n.region_x) && U32(h,n.region_y) && U32(h,n.width) && U32(h,n.height) && U32(h,n.stride) &&
        U32(h,n.codec_flags) && U32(h,static_cast<unsigned>(json.used)) && U64(h,bytes) && WriteBytes(h,json.data,json.used);
    return ok && (!valid || WriteBytes(h,pixels,bytes));
}
bool CopyRecord(HANDLE output,const wchar_t *path) {
    HANDLE input=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input==INVALID_HANDLE_VALUE)return false;
    unsigned char buffer[16384];bool ok=true;UINT64 total=0;
    for(;;){DWORD n=0;if(!ReadFile(input,buffer,sizeof(buffer),&n,nullptr)){ok=false;break;}if(!n)break;
        total+=n;if(total>40ull*1024*1024+16384 || !WriteBytes(output,buffer,n)){ok=false;break;}}
    CloseHandle(input);return ok;
}
bool WriteChunk(const wchar_t *path,const Session &s,unsigned group,unsigned chunk) {
    if(group>=s.group_count || chunk>4)return false;
    const Group &g=s.groups[group];const unsigned begin=chunk*4;
    const unsigned count=begin<g.count?std::min(chunk==4?1u:4u,g.count-begin):0;
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    bool ok=WriteBytes(h,"F8CHK004",8) && U32(h,4) && U32(h,group) && U32(h,chunk) && U32(h,count) && U32(h,16) && U32(h,5);
    for(unsigned i=begin;ok && i<begin+count;++i) {
        const auto &f=g.frames[i];
        ok=f.valid?CopyRecord(h,f.spool):WriteRecord(h,{s.id,group,i},f.info,f.params,g.requested,g.actual,nullptr,0,false);
    }
    LARGE_INTEGER size={};if(!GetFileSizeEx(h,&size) || size.QuadPart>64ll*1024*1024)ok=false;
    CloseHandle(h);if(!ok)DeleteFileW(path);return ok;
}
