// Included inside the existing Core namespace immediately before Substitution.
enum class NrPrivateImageKind : unsigned {DeferredSr=1,ResidualAcrossRr=13};
struct NrPrivateImageRequest {
    NrPrivateImageKind kind=NrPrivateImageKind::DeferredSr;
    const NgxHandle* native_handle=nullptr;
    unsigned long long feature_generation=0,source_serial=0,recording_token=0;
    unsigned configuration=0;bool selected=false;
};
// Caller owns the existing chain/native ownership locks and one EvaluateScope.
bool NrPrivateImageRequestMatches(const NrPrivateImageRequest& r,
        ID3D12GraphicsCommandList* list,const NgxHandle* handle,const FeatureRecord& feature) {
    unsigned long long token=0;
    const unsigned expected=static_cast<unsigned>(r.kind);
    return r.selected&&(expected==1||expected==13)&&feature.feature_id==static_cast<int>(expected)&&
        r.native_handle==handle&&r.feature_generation&&r.feature_generation==feature.generation&&
        r.configuration==g_config_generation.load(std::memory_order_acquire)&&
        r.source_serial&&r.source_serial==nr_source_runtime::CurrentRetryToken()&&
        ReadLifecycleToken(list,token)&&token&&token==r.recording_token;
}
struct NrPrivateImage {
    NrContext* context=nullptr;NrSlot* slot=nullptr;
    ID3D12Resource* original=nullptr;ID3D12Resource* edited=nullptr;
    NrGuideBinding guides{};ColorContract color{};
    unsigned render_width=0,render_height=0,work_width=0,work_height=0;
    unsigned origin_x=0,origin_y=0;
    unsigned long long source_serial=0,recording_token=0;
    bool ready=false;
};
// E_R already includes NR1/NR2 and user controls. Original Core recording/slot
// ownership remains authoritative: AddRef is not permission to recycle a slot.
