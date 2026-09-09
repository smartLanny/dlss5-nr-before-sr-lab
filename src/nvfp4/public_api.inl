bool Init(const Config& config) noexcept {
    try {
        auto& g=G(); std::lock_guard lock(g.mutex);
        // Reconfiguration may choose a policy, but cannot redirect already
        // loaded code to a different set of unchecked files.
        fs::path requested=config.asset_directory ? fs::path(config.asset_directory) : fs::path();
        fs::path runtime=config.nr_runtime_path ? fs::path(config.nr_runtime_path) : fs::path();
        if(config.mode!=Mode::Disabled && (requested.empty() || !requested.is_absolute())) {
            Reason(g.status,State::Fallback,"NVFP4 requires an absolute locked asset directory"); return false;
        }
        if(!g.assets.empty() && !requested.empty() && g.assets!=requested && !g.devices.empty()) {
            Reason(g.status,State::Fallback,"Asset directory change requires retiring the existing devices"); return false;
        }
        if(!g.devices.empty() && !runtime.empty() && runtime!=g.runtime_path)
            throw std::runtime_error("NR runtime identity cannot change while prepared devices are alive");
        if(!runtime.empty()) g.runtime_path=runtime;
        if(config.optimized_acceptance) {
            const auto& a=*config.optimized_acceptance;
            if(!a.directory || !fs::path(a.directory).is_absolute() || !a.cubin_sha256 || !a.builder_sha256 ||
               !a.build_id || !a.kernel_id || std::strlen(a.cubin_sha256)!=64 || std::strlen(a.builder_sha256)!=64 ||
               std::strlen(a.build_id)!=64 || !*a.kernel_id || std::strlen(a.kernel_id)>=NR_NVFP4_KERNEL_ID_CAPACITY ||
               !a.tested_m || !a.tested_m_count || a.tested_m_count>16)
                throw std::runtime_error("Incomplete optimized acceptance receipt");
            OptimizedSettings next; next.directory=a.directory; next.cubin_sha=a.cubin_sha256; next.builder_sha=a.builder_sha256;
            next.build_id=a.build_id; next.kernel_id=a.kernel_id; next.variant=a.variant; next.allow_auto=a.allow_auto; next.fused_publish=a.fused_publish;
            next.tested_m.assign(a.tested_m,a.tested_m+a.tested_m_count);
            for(auto m:next.tested_m) if(!m || m>MaximumM) throw std::runtime_error("Accepted M exceeds bounded arena policy");
            if(a.expansion) {
                const auto& e=*a.expansion;
                if(!a.fused_publish || !e.directory || !fs::path(e.directory).is_absolute() || !e.cubin_sha256 ||
                   !e.builder_sha256 || !e.build_id || !e.kernel_id || std::strlen(e.cubin_sha256)!=64 ||
                   std::strlen(e.builder_sha256)!=64 || std::strlen(e.build_id)!=64 || !*e.kernel_id ||
                   std::strlen(e.kernel_id)>=NR_EXPANSION_KERNEL_ID_BYTES || e.variant>2)
                    throw std::runtime_error("Incomplete packed expansion artifact receipt");
                next.expansion.directory=e.directory;next.expansion.cubin_sha=e.cubin_sha256;
                next.expansion.builder_sha=e.builder_sha256;next.expansion.build_id=e.build_id;
                next.expansion.kernel_id=e.kernel_id;next.expansion.variant=e.variant;
            }
            if(!g.devices.empty() && (next.build_id!=g.optimized.build_id || next.variant!=g.optimized.variant ||
                next.cubin_sha!=g.optimized.cubin_sha || next.builder_sha!=g.optimized.builder_sha || next.fused_publish!=g.optimized.fused_publish ||
                next.expansion.directory!=g.optimized.expansion.directory || next.expansion.cubin_sha!=g.optimized.expansion.cubin_sha ||
                next.expansion.builder_sha!=g.optimized.expansion.builder_sha || next.expansion.build_id!=g.optimized.expansion.build_id ||
                next.expansion.kernel_id!=g.optimized.expansion.kernel_id || next.expansion.variant!=g.optimized.expansion.variant))
                throw std::runtime_error("Optimized code receipt cannot change while devices are alive");
            g.optimized=std::move(next);
        }
        g.config=config; if(!requested.empty()) g.assets=requested;
        g.config.device_budget_bytes=std::min<uint64_t>(config.device_budget_bytes,MaximumBudget);
        auto effective=config.diagnostic_passthrough ? Mode::Passthrough : config.mode;
        // The optimized kernel must earn a validated asset/ABI identity before
        // it is admitted. A builder that merely launches does not meet that gate.
        g.mode.store(effective); g.observer_enabled.store(config.observe_module || config.observe_launch || config.observe_stage);
        g.original_observer_enabled.store(config.observe_original_launch!=nullptr);
        g.status.mode=effective;
        Reason(g.status,effective==Mode::Disabled ? State::Disabled : State::Observing,
               effective==Mode::Disabled ? "NVFP4 disabled" : "Waiting for owned pre-SR Feature18 scope");
        return true;
    } catch(...) { return false; }
}
bool ArmObserver() noexcept {
    try {
        auto& g=G();
        {
            std::lock_guard hook_lock(g.hook_mutex);
            if(!g.hook_installed) {
                if(!g.nvapi) g.nvapi=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                if(!g.nvapi) throw std::runtime_error("System NVAPI library unavailable");
                auto query=Symbol<api::Query>(g.nvapi,"nvapi_QueryInterface");
                HMODULE backend_owner=nullptr, query_owner=nullptr;
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&ArmObserver),&backend_owner) ||
                   !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(query),&query_owner))
                    throw std::runtime_error("Cannot pin NVAPI observer module owners");
                // Resolve all originals before installing the query hook, so
                // worker loads never depend on which IDs NGX requests first.
                for(uint32_t id:{api::CreateModuleId,api::CreateFunctionId,api::LaunchId,api::DestroyModuleId,api::DestroyFunctionId})
                    if(!WrapNvapi(id,query(id))) throw std::runtime_error("Required NVAPI entry unavailable");
                auto initialized=MH_Initialize();
                if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED) throw std::runtime_error("NVAPI hook engine unavailable");
                g.query_target=reinterpret_cast<void*>(query);
                if(MH_CreateHook(g.query_target,reinterpret_cast<void*>(&Query),reinterpret_cast<void**>(&g.query))!=MH_OK)
                    throw std::runtime_error("NVAPI query hook installation failed");
                if(MH_EnableHook(g.query_target)!=MH_OK) { MH_RemoveHook(g.query_target); g.query=nullptr; throw std::runtime_error("NVAPI query hook activation failed"); }
                g.hook_installed=true;
            }
        }
        return true;
    } catch(const std::exception& error) { std::lock_guard lock(G().mutex); Reason(G().status,State::Fallback,error.what()); return false; }
    catch(...) { std::lock_guard lock(G().mutex); Reason(G().status,State::Fallback,"NVAPI observer installation failed"); return false; }
}
bool EnsureStarted(ID3D12Device* device) noexcept {
    if(!device || G().mode.load()==Mode::Disabled) return false;
    if(!ArmObserver()) return false;
    try {
        auto& g=G();
        std::lock_guard lock(g.mutex);
        auto& d=g.devices[device];
        if(!d) { d=std::make_shared<Device>(); d->owner=device; d->budget->limit=g.config.device_budget_bytes; d->optimized_settings=g.optimized; d->runtime_path=g.runtime_path; }
        if(!g.worker_started) { std::thread(Worker).detach(); g.worker_started=true; }
        if(g.mode.load()!=Mode::Passthrough && (g.mode.load()!=Mode::Auto || policy::HasQualifiedPolicies())) RequestLocked(d,0);
        return true;
    } catch(const std::exception& error) { std::lock_guard lock(G().mutex); Reason(G().status,State::Fallback,error.what()); return false; }
    catch(...) { return false; }
}
void Shutdown() noexcept { G().mode.store(Mode::Disabled); G().observer_enabled.store(false); G().original_observer_enabled.store(false); }
OwnedScope::OwnedScope(const OwnedCall& call) noexcept {
    previous_=current_scope;
    // An ineligible nested scope explicitly masks an outer eligible scope.
    current_scope=nullptr;
    ScopeData* scope=nullptr;
    try {
        scope=new(storage_) ScopeData; implementation_=scope; scope->call=call; scope->status.mode=G().mode.load();
        const bool owned=call.pre_sr && call.feature_id==18 && call.context_id && call.device && call.command_list;
        scope->eligible=scope->status.mode!=Mode::Disabled && owned;
        if(!scope->eligible) {
            // A private Disabled-mode timing observer must not register a
            // device/context, start a worker, or make replacement eligible.
            if(owned && scope->status.mode==Mode::Disabled && G().original_observer_enabled.load()) {
                auto& g=G(); std::lock_guard lock(g.mutex);
                scope->observe_original_launch=g.config.observe_original_launch; scope->observer_user=g.config.observer_user;
                scope->observe_original_input_calls=g.config.observe_original_input_calls;
                scope->metadata=std::atomic_load(&g.metadata); current_scope=scope;
            }
            Reason(scope->status,State::Disabled,"Outside an enabled owned pre-SR Feature18 call"); return;
        }
        auto& g=G(); std::lock_guard lock(g.mutex);
        scope->observe_stage=g.config.observe_stage; scope->observe_launch=g.config.observe_launch; scope->observe_module=g.config.observe_module; scope->observer_user=g.config.observer_user;
        scope->observe_original_launch=g.config.observe_original_launch;
        scope->observe_original_input_calls=g.config.observe_original_input_calls;
        if(g.retired_contexts.count({call.context_id,call.generation})) {
            scope->eligible=false; Reason(scope->status,State::Fallback,"Retired context identity cannot be resurrected"); return;
        }
        auto& c=g.contexts[{call.context_id,call.generation}];
        if(!c) c=std::make_shared<Context>(); scope->context=c;
        if(c->busy.exchange(true)) { scope->eligible=false; Reason(scope->status,State::Fallback,"Concurrent calls on one NR context are not eligible"); return; }
        scope->owns_busy=true;
        { std::lock_guard status_lock(c->mutex); scope->status=c->status; scope->status.mode=g.mode.load(); }
        scope->status.replaced_pairs=0; scope->status.original_launches=0;
        scope->status.replacement_launches=0; scope->status.uav_barriers=0;
        scope->metadata=std::atomic_load(&g.metadata);
        auto device=g.devices.find(call.device);
        if(c->retired || device==g.devices.end()) { scope->eligible=false; Reason(scope->status,State::Fallback,c->retired ? "NR context is retired" : "NVAPI backend not started"); }
        else {
            c->device=device->second; scope->device=device->second; auto& d=*scope->device;
            d.ever_owned=true;
            if(scope->status.may_have_recorded_failure) { scope->failure=true; Reason(scope->status,State::Quarantined,"This context requires a new recording after failure"); }
            else if(!d.failure.empty()) Reason(scope->status,State::Fallback,d.failure.c_str());
            else if(scope->status.mode==Mode::Passthrough) Reason(scope->status,State::Observing,"Diagnostic NVAPI passthrough");
            else if(scope->status.mode==Mode::Auto && !policy::HasQualifiedPolicies())
                Reason(scope->status,State::Fallback,"Auto remains FP8 until a trusted quality and performance receipt is supplied");
            else if(!d.ready || !d.runtime_approved) Reason(scope->status,State::Preparing,"Device weights, runtime identity and kernels are preparing asynchronously");
            else if(scope->status.mode==Mode::Optimized && !d.optimized_ready) Reason(scope->status,State::Fallback,"Optimized asset/ABI numerical acceptance has not been established");
            else {
                scope->status.cuda_sm_count=d.cuda.sm; scope->status.compute_major=d.cuda.major; scope->status.compute_minor=d.cuda.minor;
                if(call.phase==Phase::Evaluate && call.recording_token && call.allow_replacement && AllowedM(d,scope->status.mode,c->m)) {
                    const bool optimized=SelectOptimized(d,scope->status.mode,c->m);
                    for(auto& a:d.arenas) if(a->ready && !a->poisoned && a->m==c->m && a->optimized==optimized &&
                        (!a->leased || (a->lifetime.token==call.recording_token && !a->lifetime.reset && a->context==call.context_id && a->generation==call.generation))) {
                        if(!a->leased) { a->leased=true; a->context=call.context_id; a->generation=call.generation; a->lifetime.token=call.recording_token; }
                        scope->arena=a; break;
                    }
                    if(!scope->arena) RequestLocked(scope->device,c->m,optimized);
                }
            }
            scope->status.live_device_bytes=d.budget->Used();
        }
        current_scope=scope;
    } catch(...) { if(scope) { scope->eligible=false; if(scope->owns_busy && scope->context) {scope->context->busy.store(false); scope->owns_busy=false;} } current_scope=nullptr; }
}
OwnedScope::~OwnedScope() {
    auto* s=static_cast<ScopeData*>(implementation_); current_scope=static_cast<ScopeData*>(previous_);
    if(!s) return;
    try {
        if(s->recorded && (s->pending || s->pair!=8)) Fail(*s,"Owned Evaluate ended with an incomplete replaced FFN sequence");
        if(s->context && s->owns_busy) {
            std::lock_guard lock(G().mutex);
            { std::lock_guard status_lock(s->context->mutex);
                s->status.completed_recordings=std::max(s->status.completed_recordings,s->context->status.completed_recordings);
                if(s->context->status.may_have_recorded_failure && !s->status.may_have_recorded_failure) {
                    s->status.may_have_recorded_failure=true; Reason(s->status,State::Quarantined,s->context->status.reason);
                }
                s->context->status=s->status;
                if(s->m && !s->failure) s->context->m=s->m;
            }
            if(s->arena && !s->recorded && s->arena->lifetime.submissions.empty()) {
                // No replacement commands were emitted by this scope. Preserve
                // a lease already used by an earlier Evaluate of the same token.
                if(!s->arena->poisoned && !s->arena->has_recording.load()) { s->arena->leased=false; s->arena->lifetime={}; }
            }
            if(s->device && s->device->ready && AllowedM(*s->device,s->status.mode,s->m) && !s->failure && !s->arena && s->status.mode!=Mode::Passthrough && s->call.allow_replacement)
                RequestLocked(s->device,s->m,SelectOptimized(*s->device,s->status.mode,s->m));
            s->context->busy.store(false);
            CollectRetiredLocked();
        }
    } catch(...) { if(s->context && s->owns_busy) s->context->busy.store(false); }
    s->~ScopeData();
}
bool OwnedScope::MayHaveRecordedFailure() const noexcept {
    auto* s=static_cast<ScopeData*>(implementation_);
    return s && (s->failure || (s->recorded && (s->pending || s->pair!=8)));
}
bool OwnedScope::WillReplace() const noexcept {
    auto* s=static_cast<ScopeData*>(implementation_);
    return s && s->eligible && !s->failure && s->call.phase==Phase::Evaluate && bool(s->arena) && s->status.mode!=Mode::Passthrough;
}
Snapshot OwnedScope::Status() const noexcept {
    auto* s=static_cast<ScopeData*>(implementation_); return s ? s->status : Snapshot{};
}
Snapshot GetStatus(uint64_t context_id, uint64_t generation) noexcept {
    try { auto& g=G(); std::lock_guard lock(g.mutex); ReapLocked();
        auto it=g.contexts.find({context_id,generation});
        if(context_id && it!=g.contexts.end()) { std::lock_guard status_lock(it->second->mutex); return it->second->status; }
        Snapshot result=g.status; result.mode=g.mode.load();
        for(auto& [key,c]:g.contexts) { (void)key; std::lock_guard status_lock(c->mutex); result=c->status; }
        result.live_device_bytes=0; for(auto& [identity,d]:g.devices) { (void)identity; result.live_device_bytes+=d->budget->Used(); }
        for(const auto& d:g.retire_queue) result.live_device_bytes+=d->budget->Used();
        for(const auto& d:g.failed_destroy_devices) result.live_device_bytes+=d->budget->Used();
        return result;
    } catch(...) { return {}; }
}
void OnSubmit(uint64_t token, ID3D12CommandQueue* queue, ID3D12Fence* fence, uint64_t value) noexcept {
    if(!token) return;
    try { std::lock_guard lock(G().mutex);
        for(auto& [identity,d]:G().devices) { (void)identity; for(auto& a:d->arenas) if(a->leased && a->lifetime.token==token) {
            if(!queue || !fence || !value) { LostRecordingLocked(a,"Submission has no trustworthy queue fence"); continue; }
            ComPtr<ID3D12Device> queue_device,fence_device;
            if(FAILED(queue->GetDevice(IID_PPV_ARGS(&queue_device))) || FAILED(fence->GetDevice(IID_PPV_ARGS(&fence_device))) ||
               queue_device.Get()!=d->owner.Get() || fence_device.Get()!=d->owner.Get()) { LostRecordingLocked(a,"Submission queue/fence device does not own this recording"); continue; }
            // Own the COM dependencies before adding their raw identities to
            // the lifetime ledger. Allocation failure cannot leave a dangling
            // fence for the worker to poll.
            if(std::none_of(a->queues.begin(),a->queues.end(),[&](auto& p){return p.Get()==queue;})) a->queues.emplace_back(queue);
            if(std::none_of(a->fences.begin(),a->fences.end(),[&](auto& p){return p.Get()==fence;})) a->fences.emplace_back(fence);
            if(!a->lifetime.Submit(reinterpret_cast<uintptr_t>(queue),reinterpret_cast<uintptr_t>(fence),value))
                LostRecordingLocked(a,"Submission arrived after Reset or discard");
        }}
        G().work.notify_one();
    } catch(...) { OnQuarantine(token,"Submission bookkeeping failed"); }
}
void OnReset(uint64_t token, bool definitely_unsubmitted) noexcept {
    if(!token) return;
    try { std::lock_guard lock(G().mutex); for(auto& [identity,d]:G().devices) { (void)identity;
        for(auto& a:d->arenas) if(a->leased && a->lifetime.token==token) a->lifetime.Reset(definitely_unsubmitted);
    } ReapLocked(); } catch(...) {}
}
void OnQuarantine(uint64_t token, const char* reason) noexcept {
    try { std::lock_guard lock(G().mutex); for(auto& [identity,d]:G().devices) { (void)identity;
        for(auto& a:d->arenas) if(a->leased && (!token || a->lifetime.token==token)) {
            LostRecordingLocked(a,reason);
        }
    }} catch(...) {}
}
void RetireContext(uint64_t context_id, uint64_t generation) noexcept {
    try { std::lock_guard lock(G().mutex); auto it=G().contexts.find({context_id,generation});
        if(it!=G().contexts.end()) it->second->retired=true;
        const Key identity{context_id,generation};
        if(G().retired_contexts.insert(identity).second) G().retirement_order.push_back(identity);
        // Identities are owner-generated and never reused. A bounded exact
        // recent-retirement guard also catches accidental immediate stale calls
        // without rejecting an older live context's delayed first entry.
        while(G().retirement_order.size()>4096) {
            G().retired_contexts.erase(G().retirement_order.front()); G().retirement_order.pop_front();
        }
        // Retirement does not imply Reset, submission, or completion.
        CollectRetiredLocked();
    } catch(...) {}
}
void* WrapNvapi(uint32_t id, void* original) noexcept {
    if(!original) return nullptr;
    auto& g=G();
    switch(id) {
    case api::CreateModuleId: if(original!=reinterpret_cast<void*>(&CreateModule)) g.create_module.store(reinterpret_cast<api::CreateModule>(original)); return reinterpret_cast<void*>(&CreateModule);
    case api::CreateFunctionId: if(original!=reinterpret_cast<void*>(&CreateFunction)) g.create_function.store(reinterpret_cast<api::CreateFunction>(original)); return reinterpret_cast<void*>(&CreateFunction);
    case api::LaunchId: if(original!=reinterpret_cast<void*>(&Launch)) g.launch.store(reinterpret_cast<api::Launch>(original)); return reinterpret_cast<void*>(&Launch);
    case api::DestroyModuleId: if(original!=reinterpret_cast<void*>(&DestroyModule)) g.destroy_module.store(reinterpret_cast<api::Destroy>(original)); return reinterpret_cast<void*>(&DestroyModule);
    case api::DestroyFunctionId: if(original!=reinterpret_cast<void*>(&DestroyFunction)) g.destroy_function.store(reinterpret_cast<api::Destroy>(original)); return reinterpret_cast<void*>(&DestroyFunction);
    default: return original;
    }
}
