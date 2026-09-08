// Internal owned Feature18 boundary. This scope is never carried into native
// Feature1/13/11 calls, nor used for a second plugin's Feature18 handle.
bool g_nvfp4_config_known = false;
unsigned g_nvfp4_config_mode = 0;
bool g_nvfp4_config_diagnostic = false;
wchar_t g_nvfp4_asset_directory[MAX_PATH] = {};
wchar_t g_nvfp4_nr_runtime_path[MAX_PATH] = {};
std::atomic<unsigned long long> g_nvfp4_recording_failures{0};
std::atomic<unsigned long long> g_nvfp4_history_resets{0};
std::atomic<unsigned long long> g_nvfp4_unconfirmed_transitions{0};

void PrepareNrPrecision(ID3D12Device *device) {
    if (!g_nvfp4_config_known || g_nvfp4_config_mode != g_config.nr_precision_mode ||
        g_nvfp4_config_diagnostic != g_config.nr_precision_diagnostic) {
        nr::nvfp4::Config config{};
        config.mode = g_config.nr_precision_mode ? nr::nvfp4::Mode::Auto : nr::nvfp4::Mode::Disabled;
        config.diagnostic_passthrough = g_config.nr_precision_diagnostic;
        SetSiblingPath(g_addon_module.load(std::memory_order_acquire), L"nvfp4-hybrid", g_nvfp4_asset_directory);
        config.asset_directory = g_nvfp4_asset_directory;
        // Hash the module actually loaded by this Core, never a same-named DLL
        // found through the current working directory or a search path.
        const DWORD length=GetModuleFileNameW(g_nr_api.signed_module,g_nvfp4_nr_runtime_path,MAX_PATH);
        if(!g_nr_api.signed_module || !length || length>=MAX_PATH)g_nvfp4_nr_runtime_path[0]=0;
        config.nr_runtime_path=g_nvfp4_nr_runtime_path;
        const bool initialized = nr::nvfp4::Init(config);
        g_nvfp4_config_known = true;
        g_nvfp4_config_mode = g_config.nr_precision_mode;
        g_nvfp4_config_diagnostic = g_config.nr_precision_diagnostic;
        Log("[nvfp4] requested=%s diagnosticPassthrough=%d init=%d; only owned pre-SR Feature18 is eligible, actual FFN replacement must be observed.",
            g_config.nr_precision_mode ? "auto" : "off-native", g_config.nr_precision_diagnostic, initialized);
    }
    if (g_config.nr_precision_mode && device) nr::nvfp4::EnsureStarted(device);
}

template<class Call>
NgxResult CallOwnedNrPrecision(NrContext &context, ID3D12GraphicsCommandList *command_list,
                              nr::nvfp4::Phase phase, Call &&call, NrSlot *slot=nullptr) {
    if (context.mode != 2 || !context.tuning_signature.precision_mode) return call();
    unsigned long long token = 0;
    if (!ReadLifecycleToken(command_list, token) || !token) return call();
    ID3D12Device *device = nullptr;
    if (FAILED(ID3D12GraphicsCommandList_GetDevice(command_list, IID_ID3D12Device,
                  reinterpret_cast<void **>(&device))) || !device) return call();
    nr::nvfp4::OwnedCall info{};
    info.phase=phase;info.context_id=context.nvfp4_context_id;
    info.generation=context.feature_generation;info.recording_token=token;
    info.evaluation_serial=phase==nr::nvfp4::Phase::Evaluate
        ? ++context.nvfp4_evaluation_serial : context.nvfp4_evaluation_serial;
    info.device=device;info.command_list=command_list;
    info.width=context.width;info.height=context.height;info.feature_id=18;info.pre_sr=true;
    info.allow_replacement=!context.nvfp4_transition_blocked;
    NgxResult result=kNgxSuccess;
    bool failed_after_recording=false;
    {
        nr::nvfp4::OwnedScope scope(info);
        // The backend freezes eligibility at scope entry. A worker becoming
        // ready mid-Evaluate must not change the precision of that recording.
        const bool planned=phase==nr::nvfp4::Phase::Evaluate && scope.WillReplace();
        const bool transition=phase==nr::nvfp4::Phase::Evaluate &&
            context.nvfp4_effective_known && planned!=context.nvfp4_last_effective;
        if(transition){
            SetInt(context.params,"DLSSNR.Reset",1);
            context.reset_retry_pending=true;context.diagnostic_reset_requested=true;
            context.nvfp4_transition_pending=true;
            if(slot){slot->capture_params.reset=true;slot->suppress_transfer_this_frame=g_config.suppress_reset_frame;}
            ++g_nvfp4_history_resets;
            Log("[nvfp4] effective precision transition planned %s->%s; NR Reset stays pending until the actual Evaluate outcome.",
                context.nvfp4_last_effective?"NVFP4":"original",planned?"NVFP4":"original");
        }
        result=call(); // Exactly one owned provider call; no FFN retry here.
        failed_after_recording=scope.MayHaveRecordedFailure();
        if(phase==nr::nvfp4::Phase::Evaluate && result==kNgxSuccess && !failed_after_recording){
            const bool actual=scope.Status().replaced_pairs!=0;
            if(actual && !planned){
                // A replacement outside the frozen plan could mix histories.
                failed_after_recording=true;
            }else if(planned && !actual){
                context.nvfp4_transition_blocked=true;
                context.nvfp4_transition_pending=true;
                ++g_nvfp4_unconfirmed_transitions;
                Log("[nvfp4] transitionUnconfirmed=1 context=%llu generation=%llu; no actual FFN pair replaced. This context stays original precision without repeated Reset; a fresh settings context may retry.",
                    info.context_id,info.generation);
            }else{
                context.nvfp4_effective_known=true;context.nvfp4_last_effective=actual;
                if(!context.nvfp4_transition_blocked)context.nvfp4_transition_pending=false;
            }
        }
    }
    ReleaseCom(device);
    if (failed_after_recording) {
        context.failed=true;
        context.lifecycle_dependencies_quarantined=true;
        g_nvfp4_failure_latch.Poison(context.feature_generation);
        ++g_nvfp4_recording_failures;
        nr::nvfp4::OnQuarantine(token,"owned Feature18 failed after possible NVFP4 recording");
        g_gpu_timing.Quarantine(token);
        Log("[nvfp4] restartRequired=1 context=%llu generation=%llu token=%llu; original SR parameters will be retained, recorded GPU work cannot be undone.",
            info.context_id,info.generation,token);
        return 0xBAD00005u;
    }
    return result;
}

nr_gpu_timing::Ticket BeginNrGpuTiming(ID3D12Device *device,
    ID3D12GraphicsCommandList *command_list, const NrContext &context) {
    if (!g_config.gpu_timing || context.mode != 2) return nr_gpu_timing::kNoTicket;
    unsigned long long token=0;
    if (!ReadLifecycleToken(command_list,token)) return nr_gpu_timing::kNoTicket;
    const nr_gpu_timing::Key key={context.nvfp4_context_id,
        g_config_generation.load(std::memory_order_relaxed),
        context.tuning_signature.precision_mode | (context.tuning_signature.precision_diagnostic ? 0x100u : 0u),
        context.width,context.height};
    return g_gpu_timing.Begin(device,command_list,token,key);
}

struct NrSrGpuTimingScope {
    nr_gpu_timing::Ticket ticket;
    ID3D12GraphicsCommandList *list;
    bool valid=false;
    NrSrGpuTimingScope(nr_gpu_timing::Ticket value,ID3D12GraphicsCommandList *command_list)
        :ticket(value),list(command_list){g_gpu_timing.BeforeSr(ticket,list);}
    ~NrSrGpuTimingScope(){g_gpu_timing.End(ticket,list,valid);}
};
