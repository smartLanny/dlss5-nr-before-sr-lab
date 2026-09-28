// Worker-only export. All filenames are generated from numeric indices.
DWORD ExportScreenshots(std::unique_ptr<Session> s){
    wchar_t html_path[1100]{},json_path[1100]{},gpu_path[1100]{};
    swprintf_s(html_path,L"%ls\\index.html",s->directory);
    swprintf_s(json_path,L"%ls\\screenshots.json",s->directory);
    swprintf_s(gpu_path,L"%ls\\performance-gpu.csv",s->directory);
    FILE *html=nullptr,*manifest=nullptr,*gpu=nullptr;
    _wfopen_s(&html,html_path,L"wb");_wfopen_s(&manifest,json_path,L"wb");
    _wfopen_s(&gpu,gpu_path,L"wb");
    bool ok=html&&manifest&&gpu;unsigned copied=0;unsigned long long total=0;
    if(gpu)std::fputs("group,config,sourceStream,sourceEpoch,sourceSequence,NRFrame,queue,fence,postCoreGpuMs\n",gpu);
    if(html)std::fprintf(html,"<!doctype html><meta charset=\"utf-8\"><title>DLSS5 comparison / 画面对比</title><style>body{font:16px sans-serif;background:#111;color:#eee}section{margin:2em 0}.pair{display:grid;grid-template-columns:1fr 1fr;gap:1em}.viewport{overflow:auto;height:70vh;border:1px solid #666}.viewport img{max-width:none;height:auto}details img{max-width:100%%;height:auto}</style><h1>DLSS5 fixed-view screenshots / 固定视角截图</h1><p>Different Present times / 不同显示时刻；not same-frame model pixels / 非同帧模型像素。ReShade encoded screenshots; HDR/raw equivalence unverified / 编码截图，不等价于 HDR 原始像素。</p><h2>Choose two images / 选择双图</h2><select id=left></select> <select id=right></select> <label>Zoom / 缩放 <input id=zoom type=range min=25 max=300 value=100></label><div class=pair><div class=viewport><img id=imga></div><div class=viewport><img id=imgb></div></div><details><summary>All groups / 全部组别</summary>");
    if(manifest)std::fprintf(manifest,"{\"schema\":\"dlss5-screenshots-v2\",\"build\":\"%s\",\"source\":\"%s\",\"restored\":%s,\"cancelled\":%s,\"gpuScope\":\"same Direct queue completed timestamp pairs around recorded post-SR Core work; excludes native SR/RR and unobserved work; whole-frame GPU unavailable\",\"groups\":[",kBuildIdentity,kSourceCommit,s->restored?"true":"false",s->cancelled?"true":"false");
    for(unsigned i=0;i<s->count;++i){
        auto& g=s->groups[i];const auto label=Get(Program::Screenshots,i,false).name;
        const char* reason=g.reason[0]?g.reason:i>s->index?"not-started":s->cancelled?"cancelled; partial":"capture attempted";
        double median=0,p95=0;
        if(g.performance_count){
            double ordered[kScreenshotGpuSamples]{};
            std::copy_n(g.performance_ms,g.performance_count,ordered);
            std::sort(ordered,ordered+g.performance_count);
            const unsigned n=g.performance_count;
            median=n&1?ordered[n/2]:(ordered[n/2-1]+ordered[n/2])*.5;
            p95=ordered[(n*95+99)/100-1];
        }
        if(gpu)for(unsigned j=0;j<g.performance_count;++j)
            std::fprintf(gpu,"%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%.9g\n",i,g.generation,
                g.performance_stream,g.performance_epoch,g.performance_sources[j],
                g.performance_frames[j],g.performance_queue,g.performance_fences[j],g.performance_ms[j]);
        if(html)std::fprintf(html,"<section><h2>%u: %s</h2><p>%s</p><div class=\"pair\">",i,label,reason);
        if(manifest)std::fprintf(manifest,"%s{\"group\":%u,\"name\":\"%s\",\"reason\":\"%s\",\"config\":%u,\"nativeRoute\":\"%s\",\"postPercent\":%u,\"performance\":{\"status\":\"%s\",\"validWarmup\":%u,\"validGpuSamples\":%u,\"rejectedSamples\":%u,\"medianMs\":%.9g,\"p95Ms\":%.9g,\"gpuTimeScope\":\"post-SR Core pre/post native call on one completed Direct queue; partial plugin scope, not whole-frame\",\"totalGpuTime\":\"unavailable\",\"wholeFrameGpuTime\":\"unavailable\",\"queue\":%llu,\"usageStartBytes\":%llu,\"usageEndBytes\":%llu,\"usageSampledPeakBytes\":%llu,\"memoryBudgetBytes\":%llu,\"memoryKnown\":%s,\"temporalGpuAdoptedDuringMeasure\":%llu,\"privateAdoptedDuringMeasure\":%llu},\"shots\":[",
            i?",":"",i,label,reason,g.generation,g.post_rr?"native-RR":"native-SR",g.percent,
            g.performance_reason[0]?g.performance_reason:"not-started",g.performance_warmup,g.performance_count,g.performance_rejected,median,p95,g.performance_queue,
            g.usage_start,g.usage_end,g.usage_sampled_peak,g.budget,g.memory_known?"true":"false",
            g.performance_temporal_adopted,g.performance_private_adopted);
        for(unsigned j=0;j<kShots;++j){
            auto& shot=g.shots[j];char file[48]{};bool saved=false;
            if(shot.received&&shot.path[0]){
                WIN32_FILE_ATTRIBUTE_DATA attrs{};const wchar_t* ext=wcsrchr(shot.path,L'.');
                const bool known=ext&&(!_wcsicmp(ext,L".png")||!_wcsicmp(ext,L".bmp")||!_wcsicmp(ext,L".jpg")||!_wcsicmp(ext,L".jpeg"));
                if(known&&GetFileAttributesExW(shot.path,GetFileExInfoStandard,&attrs)){
                    const unsigned long long size=(static_cast<unsigned long long>(attrs.nFileSizeHigh)<<32)|attrs.nFileSizeLow;
                    if(size&&size<=128ull*1024*1024&&total+size<=1024ull*1024*1024){
                        char suffix[8]{};WideCharToMultiByte(CP_UTF8,0,ext,-1,suffix,sizeof(suffix),nullptr,nullptr);
                        std::snprintf(file,sizeof(file),"group-%u-shot-%u%s",i,j,suffix);
                        wchar_t dest[1100]{};swprintf_s(dest,L"%ls\\group-%u-shot-%u%ls",s->directory,i,j,ext);
                        saved=CopyFileW(shot.path,dest,TRUE)!=FALSE;if(saved){total+=size;++copied;shot.exported=true;std::snprintf(shot.exported_file,sizeof(shot.exported_file),"%s",file);}
                    }
                }
            }
            if(html&&saved)std::fprintf(html,"<figure><img loading=lazy src=\"%s\"><figcaption>shot %u</figcaption></figure>",file,j);
            if(manifest)std::fprintf(manifest,"%s{\"index\":%u,\"saved\":%s,\"file\":\"%s\",\"status\":\"%s\",\"config\":%u,\"observedOwner\":%llu,\"observedSource\":%llu,\"sourceProof\":\"%s\",\"display\":[%u,%u],\"requestedTickMs\":%llu,\"colourEncoding\":\"ReShade encoded screenshot; transfer/primaries unverified\"}",j?",":"",j,saved?"true":"false",saved?file:"",saved?"saved":shot.received?"copy-failed-or-budget":shot.requested?"callback-or-file-missing":"not-requested",shot.config,shot.instance,shot.source,g.requested.enabled?"observation-only; no exact screenshot-frame pairing":"unknown; NR-off baseline",shot.width,shot.height,shot.tick);
        }
        if(html)std::fprintf(html,"</div></section>");if(manifest)std::fprintf(manifest,"]}");
    }
    if(html){
        std::fputs("</details><script>const shots=[",html);
        bool first=true;for(unsigned i=0;i<s->count;++i)for(unsigned j=0;j<kShots;++j){const auto& shot=s->groups[i].shots[j];if(shot.exported){std::fprintf(html,"%s{name:'%u/%u %s',file:'%s'}",first?"":",",i,j,Get(Program::Screenshots,i,false).name,shot.exported_file);first=false;}}
        std::fputs("];for(const id of ['left','right']){const e=document.getElementById(id);shots.forEach((s,i)=>e.add(new Option(s.name,i)));e.onchange=show;}if(shots.length>1)document.getElementById('right').selectedIndex=1;document.getElementById('zoom').oninput=show;function show(){const z=document.getElementById('zoom').value+'%';for(const [select,img] of [['left','imga'],['right','imgb']]){const i=document.getElementById(select).selectedIndex;const e=document.getElementById(img);e.src=i>=0?shots[i].file:'';e.style.width=z;}}show();</script>",html);
    }
    if(manifest)std::fprintf(manifest,"],\"copied\":%u,\"maxBytes\":1073741824}",copied);
    if(html)ok=std::fclose(html)==0&&ok;if(manifest)ok=std::fclose(manifest)==0&&ok;
    if(gpu)ok=std::fclose(gpu)==0&&ok;
    AcquireSRWLockExclusive(&lock);wcscpy_s(last_path,html_path);wcscpy_s(last_screenshot_page,html_path);last_restored=s->restored;
    std::snprintf(last_reason,sizeof(last_reason),"%s",!ok?"export-failed":s->cancelled?"cancelled; partial screenshots saved":copied==s->count*kShots?"screenshots saved":"partial screenshots; inspect manifest");
    packaging.store(false);ReleaseSRWLockExclusive(&lock);return 0;
}
