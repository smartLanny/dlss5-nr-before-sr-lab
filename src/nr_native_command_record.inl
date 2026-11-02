// Native object identity is independent of ReShade's optional owner callback.
// The private-data marker owns command-state observation, while a separate
// pure-storage reference pairs actual native destruction with NR ownership.
constexpr GUID kNativeCommandLifetime = {0xe39604ab,0xa7bd,0x49fe,{0xb5,0x36,0x10,0x74,0x2c,0x55,0x91,0x0a}};
std::atomic<UINT64> g_native_command_records{0}, g_native_command_record_failures{0};
std::atomic<UINT64> g_native_command_retirements{0}, g_native_command_owner_attachments{0};

bool RetireCommandStateRecord(UINT64 token, ID3D12GraphicsCommandList *native,
        reshade::api::command_list *expected_owner = nullptr,
        NativeCommandLifetime *expected_lifetime = nullptr) {
    NativeCommandState *retired=nullptr;
    bool removed=false;
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record=FindCommandStateTokenLocked(token);
    if(record && record->native==native &&
       (!expected_owner || record->owner==expected_owner) &&
       (!expected_lifetime || record->native_lifetime==expected_lifetime)) {
        if(record->prime_serial) --g_primed_command_state_count;
        retired=record->state;
        *record={};
        removed=true;
        g_command_state_epoch.fetch_add(1,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
    // COM releases can invoke other callbacks. Never run them under our lock.
    if(retired){ReleaseNativeCommandState(*retired);delete retired;}
    return removed;
}
struct NativeCommandLifetime {
    IUnknown iface;
    std::atomic<ULONG> references{1};
    std::atomic<ULONG> pod_references{1}; // marker anchor, callback and NR ledger
    const UINT64 token;
    ID3D12GraphicsCommandList *const native; // identity only; no strong reference cycle
    std::atomic<bool> native_dead{false};
    bool notifier_registered=false; // immutable after registry publication
    bool armed=false;
    NativeCommandLifetime(UINT64 captured_token, ID3D12GraphicsCommandList *captured_native)
        : token(captured_token),native(captured_native) {}
};
void NativeCommandLifetimeAddPod(NativeCommandLifetime *lifetime) {
    lifetime->pod_references.fetch_add(1,std::memory_order_relaxed);
}
void NativeCommandLifetimeDropPod(NativeCommandLifetime *lifetime) {
    if(lifetime && lifetime->pod_references.fetch_sub(1,std::memory_order_acq_rel)==1)
        delete lifetime;
}
void STDMETHODCALLTYPE NativeCommandDestroyed(void *data) {
    auto *lifetime=static_cast<NativeCommandLifetime *>(data);
    // This arbitrary native final-Release stack must not access the dying
    // object, Core locks, state/NR registries, COM, logging or vendor release.
    lifetime->native_dead.store(true,std::memory_order_release);
    NativeCommandLifetimeDropPod(lifetime);
}
bool NativeCommandLifetimeDeadMatches(const NativeCommandLifetime *lifetime,
        unsigned long long token, ID3D12GraphicsCommandList *native) {
    return lifetime && token && native && lifetime->token==token &&
        lifetime->native==native && lifetime->notifier_registered &&
        lifetime->native_dead.load(std::memory_order_acquire);
}
NativeCommandLifetime *AcquireNativeLifetimeForNr(
        ID3D12GraphicsCommandList *native, unsigned long long &token) {
    token=0;
    if(!native)return nullptr;
    const auto observed_token=ReadCommandStateToken(native);
    NativeCommandLifetime *lifetime=nullptr;
    // Callers already hold chain -> state in the established order. The
    // borrowed registry pointer is retained before releasing its lock.
    AcquireSRWLockShared(&g_command_state_lock);
    const auto *record=FindCommandStateTokenLocked(observed_token);
    if(record && record->token==observed_token && record->native==native &&
       record->native_created && record->lifetime_ready && record->native_lifetime) {
        auto *candidate=record->native_lifetime;
        if(candidate->token==observed_token && candidate->native==native &&
           candidate->notifier_registered &&
           !candidate->native_dead.load(std::memory_order_acquire)) {
            NativeCommandLifetimeAddPod(candidate);
            lifetime=candidate;token=observed_token;
        }
    }
    ReleaseSRWLockShared(&g_command_state_lock);
    return lifetime;
}
ULONG STDMETHODCALLTYPE NativeCommandLifetimeAddRef(IUnknown *iface) {
    return reinterpret_cast<NativeCommandLifetime *>(iface)->references.fetch_add(1,std::memory_order_relaxed)+1;
}
HRESULT STDMETHODCALLTYPE NativeCommandLifetimeQuery(IUnknown *iface, REFIID iid, void **output) {
    if(!output)return E_POINTER;
    *output=nullptr;
    if(!IsEqualGUID(iid,IID_IUnknown))return E_NOINTERFACE;
    *output=iface;NativeCommandLifetimeAddRef(iface);return S_OK;
}
ULONG STDMETHODCALLTYPE NativeCommandLifetimeRelease(IUnknown *iface) {
    auto *lifetime=reinterpret_cast<NativeCommandLifetime *>(iface);
    const ULONG remaining=lifetime->references.fetch_sub(1,std::memory_order_acq_rel)-1;
    if(!remaining) {
        if(lifetime->armed && RetireCommandStateRecord(
                lifetime->token,lifetime->native,nullptr,lifetime))
            g_native_command_retirements.fetch_add(1,std::memory_order_relaxed);
        NativeCommandLifetimeDropPod(lifetime);
    }
    return remaining;
}
IUnknownVtbl kNativeCommandLifetimeVtable={
    NativeCommandLifetimeQuery,NativeCommandLifetimeAddRef,NativeCommandLifetimeRelease};

bool RegisterCreatedCommandStateList(ID3D12GraphicsCommandList *native,
        const CommandStateMetaContract &meta,const CommandCreationInfo &creation) {
    if(!native || !meta.device_identity || creation.device_identity!=meta.device_identity || !creation.serial)
        return false;
    // This callback may outlive ReShade's owner. Its code must remain resident.
    static const bool pinned=PinHookAddress(reinterpret_cast<const void *>(&NativeCommandLifetimeRelease),
        "native command record lifetime",nullptr);
    if(!pinned)return false;
    UINT64 token=0;
    bool created=false;
    const auto observed_token=ReadCommandStateToken(native);
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record=FindCommandStateTokenLocked(observed_token);
    if(record) {
        if(record->native!=native || record->meta_contract.device_identity!=meta.device_identity) {
            ReleaseSRWLockExclusive(&g_command_state_lock);return false;
        }
        record->creation=creation;
        if(record->native_created && record->lifetime_ready) {
            ReleaseSRWLockExclusive(&g_command_state_lock);return true;
        }
        token=record->token;
    } else {
        for(auto &entry:g_command_states)if(!entry.token) {
            token=g_next_command_state_token++;
            if(SUCCEEDED(ID3D12GraphicsCommandList_SetPrivateData(native,kCommandStateIdentity,sizeof(token),&token))) {
                entry.token=token;entry.native=native;entry.meta_contract=meta;entry.creation=creation;
                entry.registered_tick=GetTickCount64();entry.lifetime_ready=false;
                created=true;g_command_state_epoch.fetch_add(1,std::memory_order_release);
            } else token=0;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
    if(!token){g_native_command_record_failures.fetch_add(1,std::memory_order_relaxed);return false;}
    auto *lifetime=new(std::nothrow) NativeCommandLifetime(token,native);
    bool attached=false;
    bool published=false;
    if(lifetime) {
        lifetime->iface.lpVtbl=&kNativeCommandLifetimeVtable;
        attached=SUCCEEDED(ID3D12GraphicsCommandList_SetPrivateDataInterface(native,kNativeCommandLifetime,&lifetime->iface));
        // SetPrivateDataInterface adds a reference. Do not hold our registry lock
        // across it: replacing an existing interface can invoke its destructor.
    }
    if(attached) {
        ID3DDestructionNotifier *notifier=nullptr;
        if(SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(native,
                IID_ID3DDestructionNotifier,reinterpret_cast<void **>(&notifier))) && notifier) {
            UINT callback_id=0;
            NativeCommandLifetimeAddPod(lifetime); // pre-hold pData before Register
            lifetime->notifier_registered=SUCCEEDED(
                ID3DDestructionNotifier_RegisterDestructionCallback(notifier,
                    NativeCommandDestroyed,lifetime,&callback_id));
            if(!lifetime->notifier_registered)NativeCommandLifetimeDropPod(lifetime);
        }
        AcquireSRWLockExclusive(&g_command_state_lock);
        record=FindCommandStateTokenLocked(token);
        if(record && record->native==native) {
            record->native_created=true;record->lifetime_ready=true;
            record->native_lifetime=lifetime;lifetime->armed=true;published=true;
            g_native_command_records.fetch_add(1,std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&g_command_state_lock);
        // This temporary QI must never keep the native list alive. A successful
        // callback registration owns its POD reference even if publication lost.
        if(notifier)ID3DDestructionNotifier_Release(notifier);
    } else {
        if(created)RetireCommandStateRecord(token,native);
        g_native_command_record_failures.fetch_add(1,std::memory_order_relaxed);
    }
    if(lifetime)NativeCommandLifetimeRelease(&lifetime->iface);
    // Observation remains available when QI/Register is unsupported; NR alone
    // requires notifier_registered in AcquireNativeLifetimeForNr.
    return published;
}

bool InstallCommandStateObservation(ID3D12GraphicsCommandList *native,bool observe_null) {
    ID3D12Device *device=nullptr;
    if(!native || FAILED(ID3D12GraphicsCommandList_GetDevice(native,IID_ID3D12Device,
            reinterpret_cast<void **>(&device))) || !device)return false;
    const bool ready=EnsureNativeCommandSignatureHook(device) &&
        InstallNativeCommandStateHooks(native,observe_null) &&
        EnsureNativeCommandListResetHookForNative(native);
    ID3D12Device_Release(device);
    return ready;
}

// Called only by the successful native Create observer, before returning the
// list to application recording. Receipt replay/late wrapper registration does
// not call this boundary. Only #205's initial-null ordinary-PSO case is covered.
void ObserveCommandFirstRecording(ID3D12GraphicsCommandList *native, const CommandCreationInfo &creation) {
    if (!g_enabled_fast.load(std::memory_order_acquire) || creation.method != 1 ||
        creation.initial_pso || !creation.serial || creation.flags != 0) return;
    const auto token = ReadCommandStateToken(native); // No external private-data call under this lock.
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    if (record && record->native == native && record->native_created && record->lifetime_ready &&
        record->meta_contract.device_identity == creation.device_identity &&
        record->creation.serial == creation.serial && !record->state &&
        !record->captures && !record->native_resets && !record->reset_notifications) {
        // This reuses bounded allocation and already published *complete* QI
        // family coverage. It neither installs hooks nor records a Reset.
        const auto *family = FindObservedCommandFamilyLocked(*record, native);
        if (family && family->null_guard) PrimeCommandListBeforeResetLocked(*record, native);
        if (record->state && record->hooks_ready && record->state->null_guard_covered) {
            record->state->first_recording_observed = true;
            record->state->unknown_reason = "create-awaiting-PSO";
            // Do not use ResetNativeCommandState: reset_serial and native Reset
            // evidence must stay zero. The dummy initial PSO is not restorable.
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
}
