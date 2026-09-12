// Bounded, coherent receipt-producer diagnostics. No pointer values or raw logs.
// This is independent from Feature18 success/visibility counters.
struct GuideCaptureDiagnostic {
    Source source{};
    unsigned config=0,depth_physical_width=0,depth_physical_height=0;
    unsigned depth_x=0,depth_y=0,depth_width=0,depth_height=0;
    unsigned motion_width=0,motion_height=0,output_width=0,output_height=0;
    bool depth_state_observed=false,motion_state_observed=false,shader_snapshot=false;
    const char* stage="unobserved";
};
struct GuideReceiptDiagnostics {
    GuideCaptureDiagnostic last{};
    Id attempts=0,recorded=0,native_success=0,native_failure=0;
    Id submitted=0,signal_failure=0,selected=0;
    unsigned present_width=0,present_height=0;
    nr_late_present::Decision selection=nr_late_present::Decision::None;
};
GuideReceiptDiagnostics guide_receipt_diagnostics{};
thread_local GuideCaptureDiagnostic* active_guide_capture=nullptr;
struct GuideCaptureAttempt {
    GuideCaptureDiagnostic value{};
    GuideCaptureDiagnostic* previous=nullptr;
    bool owner=false;
    explicit GuideCaptureAttempt(Source source):previous(active_guide_capture),owner(previous==nullptr) {
        if(owner){value.source=source;value.config=g_config_generation.load(std::memory_order_relaxed);
            value.stage="entry";active_guide_capture=&value;}
    }
    ~GuideCaptureAttempt(){
        if(!owner)return;
        {Lock lock;guide_receipt_diagnostics.last=value;++guide_receipt_diagnostics.attempts;
            if(std::strcmp(value.stage,"recorded")==0)++guide_receipt_diagnostics.recorded;}
        active_guide_capture=previous;
    }
};
void GuideCaptureStage(const char* stage){if(active_guide_capture)active_guide_capture->stage=stage;}
bool GuideCaptureDecline(const char* stage){GuideCaptureStage(stage);return false;}
bool TryGuideReceiptDiagnostics(GuideReceiptDiagnostics& out){
    out={};if(!TryAcquireSRWLockShared(&source_lock))return false;
    out=guide_receipt_diagnostics;ReleaseSRWLockShared(&source_lock);return true;
}
