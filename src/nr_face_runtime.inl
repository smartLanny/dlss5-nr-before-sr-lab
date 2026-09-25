namespace nr_face_runtime {
nr_face::Worker* worker=nullptr;
nr_face::Reference* reference=nullptr;
nr_face::Result detection{};
std::shared_ptr<nr_face_gpu::Texture> history[8]{};
nr_region::Frame current{};
std::uint64_t epoch=0,last_detection_request=0;
bool worker_requested=false;
std::atomic<unsigned> state{static_cast<unsigned>(nr_region::Status::Off)},detected_faces{0},confirmed_mask_pixels{0};
std::atomic<float> detector_cpu_ms{0};
std::atomic<std::uint64_t> confirmed_frame{0};
void State(nr_region::Status value){state.store(static_cast<unsigned>(value),std::memory_order_relaxed);}
bool Requested(){return g_config.enabled&&g_config.face_protection_version==1&&g_config.face_protection;}
void ClearHistory() {
    for(auto& entry:history)entry.reset();
    detection={};detected_faces.store(0);confirmed_mask_pixels.store(0);confirmed_frame.store(0);last_detection_request=0;current={};++epoch;
}
void StopWorker() {
    if(worker){worker->Stop();delete worker;worker=nullptr;}
    delete reference;reference=nullptr;worker_requested=false;ClearHistory();State(nr_region::Status::Off);
}
bool StartWorker() {
    if(worker_requested)return worker&&worker->Accepting();
    worker_requested=true;
#if NR_FACE_BACKEND
    try {
        std::wstring directory=g_ini_path;const auto slash=directory.find_last_of(L"\\/");
        if(slash==std::wstring::npos)return false;
        directory.resize(slash+1);directory+=L"nr_face/";
        worker=new(std::nothrow)nr_face::Worker;
        reference=new(std::nothrow)nr_face::Reference;
        if(worker&&reference&&worker->Start(directory+L"onnxruntime.dll",directory+L"yunet-dynamic.onnx")) {
            State(nr_region::Status::Waiting);return true;
        }
    }catch(...){}
#endif
    if(worker){delete worker;worker=nullptr;}delete reference;reference=nullptr;
    State(nr_region::Status::WorkerUnavailable);return false;
}
void Collect() {
    if(!Requested()&&worker_requested)StopWorker();
    if(worker) {
        nr_face::Result result{};
        if(worker->Take(result)) {
            detector_cpu_ms.store(static_cast<float>(result.cpu_ms));
            if(result.status==nr_region::Status::WorkerUnavailable){detection={};State(result.status);}
            else if(nr_region::SameContract(result.frame,current)&&result.frame.frame<=current.frame&&
                    current.frame-result.frame.frame<=nr_region::kMaxHistory) {
                detection=result;detected_faces.store(result.count);State(result.status);
            }
        }
    }
    if(!tracker||g_lifecycle_tracking_uncertain.load())return;
    tracker->Collect([](void* key,std::uint64_t value) {
        auto* queue=static_cast<LifecycleQueue*>(key);if(!queue||!queue->fence||!value)return false;
        const auto completed=ID3D12Fence_GetCompletedValue(queue->fence);return completed!=UINT64_MAX&&completed>=value;
    },[](const nr_region::Frame& frame,ID3D12Resource* readback,unsigned width,unsigned height) {
        if(!worker||!reference||!Requested()||!nr_region::SameContract(frame,current)||frame.frame>current.frame||current.frame-frame.frame>6)return;
        const auto bytes=width*height*4ull+16;const D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};
        void* mapped=nullptr;if(FAILED(ID3D12Resource_Map(readback,0,&range,&mapped)))return;
        const auto* data=static_cast<const std::uint32_t*>(mapped);const auto pixels=width*height;
        if(frame.frame>confirmed_frame.load()) {
            confirmed_frame.store(frame.frame);confirmed_mask_pixels.store(data[pixels+2]);
            if(detection.count&&data[pixels+2]==0)State(nr_region::Status::LowConfidence);
        }
        // A complete reference every three evaluated frames, with queue
        // replacement if the detector is busy; interval remains experimental.
        if(frame.frame>=last_detection_request+3 || last_detection_request==0) {
            reference->frame=frame;reference->width=width;reference->height=height;
            for(unsigned i=0;i<pixels;++i) {
                reference->bgr[i*3]=static_cast<std::uint8_t>(data[i]);
                reference->bgr[i*3+1]=static_cast<std::uint8_t>(data[i]>>8);
                reference->bgr[i*3+2]=static_cast<std::uint8_t>(data[i]>>16);
            }
            if(worker->Submit(*reference))last_detection_request=frame.frame;
        }
        const D3D12_RANGE none{0,0};ID3D12Resource_Unmap(readback,0,&none);
    });
}
void Submit(std::uint64_t token,void* queue,std::uint64_t fence){if(tracker)tracker->Submit(token,queue,fence);}
void Invalidate(std::uint64_t token,bool unsubmitted){if(tracker)tracker->Invalidate(token,unsubmitted);}
unsigned Begin(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
               const NrGuideBinding& guides,const FeatureRecord& feature,unsigned width,unsigned height,unsigned x,unsigned y) {
    slot.face_tracker_index=nr_face_gpu::Tracker::Invalid;
    // These are private descriptors, not the heap already recorded for proxy
    // generation. The NR slot's existing mutation gate proves old consumers retired.
    ReleaseCom(slot.face_mask);
    if(!Requested())return nr_face_gpu::Tracker::Invalid;
    if(!StartWorker()){State(nr_region::Status::WorkerUnavailable);return nr_face_gpu::Tracker::Invalid;}
    if(!EnsureFaceCodec()){State(nr_region::Status::ResourceUnavailable);return nr_face_gpu::Tracker::Invalid;}
    if(!tracker)tracker=new(std::nothrow)nr_face_gpu::Tracker;
    if(!tracker||!tracker->Ensure(g_codec.device)){State(nr_region::Status::ResourceUnavailable);return nr_face_gpu::Tracker::Invalid;}
    nr_region::Frame next{};next.context=context.instance_id;next.epoch=epoch?epoch:1;next.frame=std::uint64_t(context.evaluations)+1;
    next.width=width;next.height=height;next.x=x;next.y=y;next.model_width=context.width;next.model_height=context.height;
    next.route=context.mode==2?nr_region::Route::BeforeSr:(feature.feature_id==13?nr_region::Route::AfterRr:nr_region::Route::AfterSr);
    next.color=nr_region::ColorDomain::ModelEncoded709;
    next.exposure_contract=static_cast<unsigned>(slot.codec_color_mode)+1;
    if(context.diagnostic_reset_requested||!nr_region::SameContract(next,current)||next.frame!=current.frame+1||
        (history[0]&&(!history[0]->valid||history[0]->identity.frame+1!=next.frame))) {
        ClearHistory();next.epoch=epoch;
    }
    current=next;
    if(!next.Valid()){State(nr_region::Status::InvalidExtent);return nr_face_gpu::Tracker::Invalid;}
    nr_face_gpu::Inputs inputs{};inputs.proxy=slot.proxy;
    if(!GetResource(context.params,"DLSSNR.MVec",&inputs.motion)||!GetResource(context.params,"DLSSNR.Depth",&inputs.depth)||!inputs.motion||!inputs.depth) {
        State(nr_region::Status::MissingMotion);return nr_face_gpu::Tracker::Invalid;
    }
    inputs.motion_format=nr_replay::ReadFormat(ID3D12Resource_GetDesc(inputs.motion).Format);
    inputs.depth_format=nr_replay::ReadFormat(ID3D12Resource_GetDesc(inputs.depth).Format);
    nr_face_gpu::Constants c{};
    c.width=320;c.height=std::clamp(((height*320/width+31)/32)*32,32u,224u);
    if(height*320/width>224)c.width=std::clamp(((width*224/height+31)/32)*32,32u,320u);
    c.proxy_width=context.width;c.proxy_height=context.height;
    GetUInt(context.params,"DLSSNR.MVecSubrectWidth",&c.motion_width);GetUInt(context.params,"DLSSNR.MVecSubrectHeight",&c.motion_height);
    GetUInt(context.params,"DLSSNR.DepthSubrectWidth",&c.depth_width);GetUInt(context.params,"DLSSNR.DepthSubrectHeight",&c.depth_height);
    GetUInt(context.params,"DLSSNR.MVecSubrectBaseX",&c.motion_x);GetUInt(context.params,"DLSSNR.MVecSubrectBaseY",&c.motion_y);
    GetUInt(context.params,"DLSSNR.DepthSubrectBaseX",&c.depth_x);GetUInt(context.params,"DLSSNR.DepthSubrectBaseY",&c.depth_y);
    float sx=0,sy=0;
    c.motion_valid=guides.valid_motion_scale_x&&guides.valid_motion_scale_y&&
        GetFloat(context.params,"DLSSNR.MVecScaleX",&sx)&&GetFloat(context.params,"DLSSNR.MVecScaleY",&sy);
    c.motion_scale_x=sx/context.width;c.motion_scale_y=sy/context.height;
    c.depth_valid=guides.signature.rr_depth_placeholder?0:1;c.reset=context.diagnostic_reset_requested?1:0;
    const bool jitter_known=guides.jitter.valid_x&&guides.jitter.valid_y&&guides.jitter.source_width&&guides.jitter.source_height;
    const float jx=jitter_known?guides.jitter.source_x/guides.jitter.source_width:0;
    const float jy=jitter_known?guides.jitter.source_y/guides.jitter.source_height:0;
    const bool mv_jittered=(guides.signature.normalized_create_flags&4)!=0;
    const bool needs_jitter=context.mode==2?!mv_jittered:mv_jittered;
    if(context.mode!=2 && mv_jittered)c.motion_valid=0; // Raw game jitter is not preserved by the post adapter.
    c.has_previous=history[0]&&history[0]->valid?1:0;
    if(needs_jitter&&c.has_previous) {
        if(!jitter_known||!history[0]->jitter_known)c.motion_valid=0;
        else {const float sign=context.mode==2?1.f:-1.f;c.jitter_delta_x=(history[0]->jitter_x-jx)*sign;c.jitter_delta_y=(history[0]->jitter_y-jy)*sign;}
    }
    for(unsigned i=0;i<8;++i)inputs.history[i]=history[i];
    nr_face_gpu::Face faces[16]{};
    if(detection.status==nr_region::Status::Available&&nr_region::SameContract(detection.frame,next)&&next.frame>=detection.frame.frame&&next.frame-detection.frame.frame<=8) {
        c.face_age=static_cast<unsigned>(next.frame-detection.frame.frame);
        bool complete=c.motion_valid!=0;
        for(unsigned i=0;i<c.face_age;++i)complete=complete&&history[i]&&history[i]->valid&&history[i]->identity.frame==next.frame-i-1;
        nr_region::Input region{};region.source=nr_region::Source::BeforeNrDetector;
        region.reference=detection.frame;region.confidence=1;region.coordinates_verified=true;
        region.proven_history_steps=complete?c.face_age:0;
        for(unsigned i=0;i<detection.count;++i)region.confidence=std::min(region.confidence,detection.faces[i].confidence);
        complete=complete&&region.For(next)==nr_region::Status::Available;
        if(complete) {
            c.face_count=detection.count;
            for(unsigned i=0;i<c.face_count;++i) {
                const auto& f=detection.faces[i];auto* v=faces[i].values;
                v[0]=f.x;v[1]=f.y;v[2]=f.width;v[3]=f.height;
                std::copy_n(f.landmarks,10,v+4);v[14]=f.confidence;
            }
        } else {c.face_age=0;State(nr_region::Status::MissingMotion);}
    }else if(detection.count)State(nr_region::Status::Stale);
    if(!c.motion_valid)State(nr_region::Status::MissingMotion);
    const auto index=tracker->Arm(next,c.width,c.height);
    if(index==nr_face_gpu::Tracker::Invalid||!tracker->Record(index,list,context.pending_recording_token,inputs,c,faces,IsSameRrDepthDevice)) {
        tracker->Discard(index);State(nr_region::Status::ResourceUnavailable);return nr_face_gpu::Tracker::Invalid;
    }
    slot.face_tracker_index=index;
    // Descriptor access is enabled only once a detection and every history
    // step exist. Pixel/scene rejection still executes in the GPU mask pass.
    if(c.face_count||g_config.face_preview) {
        slot.face_mask=tracker->Mask(index);if(slot.face_mask)ID3D12Resource_AddRef(slot.face_mask);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=kCodecDescriptorCount;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(!slot.face_codec_heap&&FAILED(ID3D12Device_CreateDescriptorHeap(g_codec.device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&slot.face_codec_heap)))) {
            ReleaseCom(slot.face_mask);State(nr_region::Status::ResourceUnavailable);
        }else WriteCodecDescriptorSet(g_codec.device,slot,slot.face_codec_heap);
    }
    return index;
}
void Commit(NrSlot& slot,const NrGuideBinding& guides) {
    if(!tracker||slot.face_tracker_index==nr_face_gpu::Tracker::Invalid)return;
    auto guide=tracker->Commit(slot.face_tracker_index);if(!guide)return;
    guide->jitter_known=guides.jitter.valid_x&&guides.jitter.valid_y&&guides.jitter.source_width&&guides.jitter.source_height;
    guide->jitter_x=guide->jitter_known?guides.jitter.source_x/guides.jitter.source_width:0;
    guide->jitter_y=guide->jitter_known?guides.jitter.source_y/guides.jitter.source_height:0;
    for(unsigned i=7;i>0;--i)history[i]=std::move(history[i-1]);history[0]=std::move(guide);
}
void DrawUi(ChainConfig& next,bool& changed) {
    const bool zh=UseChineseUi();
    ImGui::SeparatorText(zh?"独立人脸保护（实验）":"Independent face protection (experimental)");
    if(!NR_FACE_BACKEND) {
        ImGui::TextWrapped("%s",zh?"此构建未包含识别后端；人脸保护不可用。":"This build has no detection backend; face protection is unavailable.");return;
    }
    if(ImGui::Checkbox(zh?"启用独立脸区":"Enable independent face region",&next.face_protection)){if(!next.face_protection)next.face_preview=0;changed=true;}
    ImGui::BeginDisabled(!next.face_protection);
    changed|=ImGui::Checkbox(zh?"脸内额外增强限制为 1 倍":"Cap extra face enhancement at 1x",&next.face_extra_cap);
    float luma=next.face_original_luma*100,color=next.face_original_color*100;
    ImGui::TextWrapped("%s",zh?"脸部原图明暗保护":"Original face luma protection");ImGui::SetNextItemWidth(-1.f);
    if(ImGui::SliderFloat("##face-original-luma",&luma,0,100,"%.0f%%")){next.face_original_luma=luma/100;changed=true;}
    ImGui::TextWrapped("%s",zh?"脸部原图颜色保护":"Original face colour protection");ImGui::SetNextItemWidth(-1.f);
    if(ImGui::SliderFloat("##face-original-colour",&color,0,100,"%.0f%%")){next.face_original_color=color/100;changed=true;}
    bool preview=next.face_preview!=0;
    if(ImGui::Checkbox(zh?"预览当前可信脸区":"Preview current trusted face region",&preview)){next.face_preview=preview?1:0;changed=true;}
    ImGui::EndDisabled();
    if(!next.face_protection)ImGui::TextWrapped("%s",zh?"独立检测已关闭；保留参数，已提交工作安全收尾。":"Independent detection is off; settings retained and submitted work drains safely.");
    ImGui::TextWrapped("%s",zh?"随包资源占安装空间；关闭时仅保留少量状态检查，不启动检测模型或新脸区处理。与每层原生皮肤遮罩无联动。":"Bundled resources use disk space. Off keeps small state checks without starting the detector or new face work; native layer skin masks are independent.");
    static const char* en[]={"Off","Waiting for detection","Face region available","No face detected","Detection expired","Input contract changed","Motion history unavailable","Depth unavailable","Unverified region source","Region rejected by correspondence checks","Invalid extent","Detector unavailable","GPU resources unavailable"};
    static const char* cn[]={"关闭","等待识别","脸区可用","未识别人脸","识别结果已过期","输入合同变化","运动历史不可用","深度不可用","区域来源未验证","脸区未通过对应检查","范围无效","识别器不可用","GPU资源不可用"};
    const auto status=next.face_protection?std::min(state.load(),static_cast<unsigned>(std::size(en)-1)):0u;
    ImGui::TextWrapped(zh?"%s；最近检测 %u 张脸；最近确认脸区 %u 像素":"%s; last detection %u faces; last confirmed mask %u pixels",zh?cn[status]:en[status],detected_faces.load(),confirmed_mask_pixels.load());
    ImGui::TextWrapped(zh?"最近检测 CPU %.2f ms；非整帧 GPU 开销":"Last detection CPU %.2f ms; not whole-frame GPU overhead",detector_cpu_ms.load());
    ImGui::TextWrapped("%s",zh?"不修改模型的人脸参数。两项原图保护均为100%时，脸内恢复当前合成节点的原输入。实际效果与整帧开销需游戏对照；也可能识别到界面肖像。":"Model Skin settings are unchanged. Both original protections at 100% restore this composition node's input inside the face. Quality and whole-frame cost need game comparison; UI portraits may also be detected.");
}
}
