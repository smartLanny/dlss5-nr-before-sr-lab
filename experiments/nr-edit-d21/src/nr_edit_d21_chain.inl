// D21 actual model chain; called under the Core chain lock. Not a new owner.
using AppliedChain=nr_edit_chain::Applied<nr_full::kMaxPasses>;
using EditHistory=nr_edit_chain::History<nr_full::kMaxPasses>;
std::atomic<unsigned> actual_chain_used{0},actual_chain_requested{0};
std::atomic<Id> chain_reset_requests{0};
std::atomic<const char*> chain_history_reason{"no-edit-sample"};
bool ReadAppliedChain(const NrPrivateImage& image,AppliedChain& out){
    out={};const auto* primary=image.context;
    if(!image.ready||!primary||!image.slot||primary->retired||primary->failed||!primary->instance_id||!primary->nr_handle)return false;
    out.configuration=g_config_generation.load(std::memory_order_acquire);
    const bool full=g_config.nr_passes==2&&g_config.nr_full_reference;
    out.requested=full?g_config.nr_full_passes:g_config.nr_passes==2?2u:1u;
    out.used=1;
    out.layers[0]={primary->instance_id,primary->history_generation,primary->width,primary->height,primary->diagnostic_reset_requested};
    const auto* selected=image.slot->dual_selected;
    if(!selected)return out.Valid();
    const auto* last=selected->dual_owner;
    if(!last||last->retired||last->failed||!last->nr_handle||last->parent_instance_id!=primary->instance_id)return false;
    if(!full){
        if(out.requested!=2||last->layer_index!=1)return false;
        out.used=2;
        out.layers[1]={last->instance_id,last->history_generation,last->width,last->height,last->diagnostic_reset_requested};
        return out.Valid();
    }
    if(out.requested>nr_full::kMaxPasses||out.requested<2||last->layer_index!=out.requested-1||
       nr_dual_runtime::full_generation!=out.configuration||nr_dual_runtime::full_root!=primary->instance_id||
       nr_dual_runtime::full_adopted.load(std::memory_order_relaxed)!=out.requested)return false;
    out.used=out.requested;
    for(const auto& context:g_contexts){
        const unsigned i=context.layer_index;
        if(i==0||i>=out.used||context.parent_instance_id!=primary->instance_id||
           context.instance_id!=nr_dual_runtime::full_instance[i].load(std::memory_order_relaxed))continue;
        if(context.retired||context.failed||!context.nr_handle||out.layers[i].instance)return false;
        out.layers[i]={context.instance_id,context.history_generation,context.width,context.height,context.diagnostic_reset_requested};
    }
    return out.Valid()&&out.layers[out.used-1].instance==last->instance_id;
}
