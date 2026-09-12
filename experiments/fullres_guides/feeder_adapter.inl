// Included by the staged official Feeder after its D3D12 helpers. This is the
// single-owner integration: the Feeder captures Color/Depth, this adapter
// substitutes synthetic OF motion, and the same NRExternalProviderV1 owner
// performs the only Core Submit. The standalone addon is a development host and
// must not be installed beside this integrated Feeder variant.
namespace nr_fullres_feeder_adapter {
enum class Result { NotSelected, Declined, Delivered };

struct Settings { bool enabled=false; nr::fullres::Backend backend=nr::fullres::Backend::amd; nr::fullres::FlowScale scale=nr::fullres::FlowScale::full; };
struct State {
    Settings settings{};
    std::unique_ptr<nr::fullres::Session> session;
    bool announced=false;
};
static State state{};

static Settings ReadSettings() {
    Settings s{};s.enabled=g_cfg.of_enabled!=0;
    s.backend=g_cfg.of_backend==2?nr::fullres::Backend::nvidia:nr::fullres::Backend::amd;
    s.scale=g_cfg.of_scale==50?nr::fullres::FlowScale::half:nr::fullres::FlowScale::full;
    return s;
}
static bool Same(Settings a,Settings b){return a.enabled==b.enabled&&a.backend==b.backend&&a.scale==b.scale;}

// FeedFrame12 enters with a fresh private list. Once OF is selected every exit
// consumes that list exactly once, even when the vendor session is unavailable.
// This keeps queue-affinity training real and prevents the ordinary-motion V1
// path from becoming a second submit owner for the same frame.
static Result DeclineOpened() {
    (void)EndCommands();
    return Result::Declined;
}

static bool Retire() {
    if(state.session){
        state.session->poll();
        if(!state.session->safe_to_destroy())return false;
        state.session.reset();
    }
    state.settings={};state.announced=false;
    return true;
}

static Result Submit(ID3D12Resource* color,D3D12_RESOURCE_STATES color_state,
                     ID3D12Resource* depth,D3D12_RESOURCE_STATES depth_state,
                     ID3D12Resource* output,D3D12_RESOURCE_STATES output_state,
                     bool inverted,bool reset) {
    const auto next=ReadSettings();
    if(!Same(next,state.settings)){
        if(state.session){state.session->poll();if(!state.session->safe_to_destroy())return DeclineOpened();state.session.reset();}
        state.settings=next;state.announced=false;
    }
    if(!state.settings.enabled){
        if(state.session){state.session->poll();if(!state.session->safe_to_destroy())return DeclineOpened();state.session.reset();}
        return Result::NotSelected;
    }
    if(!color||!depth||!output||!g.dev12||!g.list||!g.queue||!g.fence12)return DeclineOpened();
    const auto c=color->GetDesc(),d=depth->GetDesc(),o=output->GetDesc();
    if(c.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||
       o.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||c.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
       d.Format!=DXGI_FORMAT_R32_FLOAT||o.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
       c.Width!=g.width||c.Height!=g.height||d.Width!=g.width||d.Height!=g.height||
       o.Width!=g.width||o.Height!=g.height){
        if(!state.announced){Log("[feed] OF V1 experiment requires same-size RGBA8 Color/Output and R32 Depth; retaining the original frame");state.announced=true;}
        return DeclineOpened();
    }
    if(!state.session){
        state.session=std::make_unique<nr::fullres::Session>(g.dev12,state.settings.backend,
            nr::fullres::Extent{g.width,g.height},state.settings.scale);
        if(!state.session->initialized()){
            Log("[feed] OF V1 session unavailable: %s",state.session->status());return DeclineOpened();
        }
        Log("[feed] OF V1 single-owner path enabled: backend=%s scale=%u%%",
            state.settings.backend==nr::fullres::Backend::amd?"AMD":"NVIDIA",static_cast<unsigned>(state.settings.scale));
    }
    state.session->poll();
    if(state.session->faulted()||!state.session->idle())return DeclineOpened();
    state.session->mark_capture_recorded();
    Barrier(color,color_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(depth,depth_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    g.list->CopyResource(state.session->snapshot(),color);
    g.list->CopyResource(state.session->depth_snapshot(),depth);
    Barrier(color,D3D12_RESOURCE_STATE_COPY_SOURCE,color_state);
    Barrier(depth,D3D12_RESOURCE_STATE_COPY_SOURCE,depth_state);
    const UINT64 captured=EndCommands();
    if(!captured){state.session->poison("Feeder capture command list did not reach its exact fence");return Result::Declined;}
    const nr::fullres::Frame frame{1,nr_external_resource_epoch,++nr_external_frame_seq,{g.width,g.height},reset};
    nr::fullres::Delivery delivery{};
    if(!state.session->submit(g.fence12,captured,frame,inverted,delivery,output,output_state))return Result::Declined;
    if(!delivery.writeback_recorded)return Result::Declined;
    if(FAILED(g.queue->Wait(delivery.completion,delivery.value))){
        state.session->poison("Feeder queue could not wait for Core OF completion");return Result::Declined;
    }
    return Result::Delivered;
}
} // namespace nr_fullres_feeder_adapter
