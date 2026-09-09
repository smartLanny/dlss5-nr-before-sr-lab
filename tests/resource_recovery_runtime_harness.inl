// Included after the complete frozen production Core translation unit and the
// unchanged queue, post-output, native-view and PRE-source fixture helpers.
// Vendor callbacks will be scripted at the existing NGX boundary. Descriptors,
// RuntimeRecovery, shader dispatch, Execute/fences and stored reads remain real.
// Results distinguish callback-result wiring from NVIDIA model validation.
#define NR_RECOVERY_RUNTIME_FIXTURE_API_READY 1

namespace resource_recovery_runtime_test {
using queue_test::Check;
using queue_test::Hr;
using native_color_test::Texture;
using post_output_test::ReadTexture;
unsigned nr_test_double_calls = 0, native_test_double_calls = 0;
constexpr NgxResult kScriptedNrFailure = 0xBAD0000Du;
constexpr NgxResult kScriptedNativeFailure = 0xBAD0000Eu;

enum class Fault { None, MissingDescriptor, NrFailure, NativeFailure, RecordOnly, WrongTarget };
struct Scenario {
    const char* name;
    Fault fault;
    const char* required_boundary;
};
constexpr Scenario scenarios[] = {
    {"cold-auto-recover", Fault::None, "unknown -> native SRV28 -> next carrier -> three complete carriers -> actual Result proof"},
    {"missing-descriptor", Fault::MissingDescriptor, "omit actual descriptor receipt; no Succeeded"},
    {"nr-failure", Fault::NrFailure, "production NGX callback returns original error; latch and no Succeeded"},
    {"native-failure", Fault::NativeFailure, "native SR callback returns original error; no Succeeded"},
    {"record-only", Fault::RecordOnly, "recorded transfer without Execute receipt; no Succeeded"},
    {"wrong-target", Fault::WrongTarget, "redirect actual Result UAV while stored probe still reads bound Result; no Succeeded"},
};

// This setup already exercises actual object lifetimes and descriptor hooks.
// It will be consumed by the RuntimeRecovery calls once their production API
// is available; it never inserts observations into a test-owned policy ledger.
struct ColdScene {
    queue_test::Fixture& fixture;
    Texture original;
    unsigned long long generation = 0;
    NrSlot* slot = nullptr;
    explicit ColdScene(queue_test::Fixture& f) : fixture(f) {
        generation = ++native_color_test::next_generation;
        native_color_test::ResetContext(false, generation, 8, 6);
        queue_test::context.evaluations = 0;
        queue_test::context.instance_id = generation + 100000;
        queue_test::context.config_generation = static_cast<unsigned>(generation);
        g_config_generation = queue_test::context.config_generation;
        g_requested_mode_fast.store(2);
        native_color_test::MakeTexture(fixture, original, DXGI_FORMAT_R8G8B8A8_TYPELESS, false);
        queue_test::List list(fixture);
        NrPrepareFormatObservation(list.native);
        Check(NrFormatGetDevice(fixture.device).hooks,
              "production native format hooks installed on the actual WARP device");
        list.Close();
    }
    NrResolvedColorView Resolve() {
        return NrResolveColorView(fixture.device, original.resource,
            ID3D12Resource_GetDesc(original.resource), generation, false, 0);
    }
    void AssertCold() {
        const auto view = Resolve();
        Check(!view && view.semantic.source == nr_resource_semantics::Source::None,
              "cold typeless input has no invented interpretation");
        Check(!FindOrCreateSlot(queue_test::context, fixture.device, original.resource, false,
                  nullptr, CodecColorMode::ModelEncodedValueDomain, 0),
              "cold actual Core slot admission preserves native-only rendering");
    }
    void ObserveNative28(NgxResult status = kNgxSuccess) {
        native_color_test::Observe(fixture, original, false, generation,
                                  DXGI_FORMAT_R8G8B8A8_UNORM, status);
    }
    void AdmitNextCarrier() {
        const auto view = Resolve();
        Check(static_cast<bool>(view) && view.semantic.source == nr_resource_semantics::Source::ObservedNativeView &&
              view.plan.srv == 28, "next regular carrier resolves the actually observed native SRV28");
        slot = FindOrCreateSlot(queue_test::context, fixture.device, original.resource, false,
                               nullptr, CodecColorMode::ModelEncodedValueDomain, 0);
        Check(slot && slot->source_srv_format == DXGI_FORMAT_R8G8B8A8_UNORM &&
              slot->owns_result && slot->result != original.resource && slot->result_format == DXGI_FORMAT_R16G16B16A16_FLOAT,
              "production admission allocates a distinct owned FP16 Result with the observed input view");
    }
    void RetireAfterDrainAndCollection(bool collect = true) {
        fixture.Drain(fixture.a);
        if (collect && slot) CollectSlotDeltaStatsLocked(queue_test::context, *slot);
        NrRecoveryRetireFeature(generation);
        for (auto& value : queue_test::context.slots) DestroySlot(value);
        slot = nullptr;
    }
};

nr_recovery::RecoveryEvidence Read(const nr_recovery::Token& token) {
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto result = g_nr_recovery.Read(token.key, token.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    return result;
}

// This bootstrap reads actual D3D12 capabilities. Native Create/setter hooks,
// lifetime records, PSO state and restoration are the production implementation.
// No record is marked known or assigned a synthetic successful Reset.
void InstallCommandObservation(queue_test::Fixture& fixture) {
    CommandStateMetaContract meta;
    ID3D12Device5* extended = nullptr;
    const auto queried = ID3D12Device_QueryInterface(fixture.device, IID_ID3D12Device5,
                                                    reinterpret_cast<void**>(&extended));
    if (queried == E_NOINTERFACE) meta.known = true;
    else {
        Hr(queried, "query actual device meta-command interface");
        UINT count = 0;
        Hr(ID3D12Device5_EnumerateMetaCommands(extended, &count, nullptr), "actual meta-command count");
        Check(count <= 256, "bounded actual meta-command capabilities");
        std::vector<D3D12_META_COMMAND_DESC> descriptions(count);
        UINT actual = count;
        if (count) Hr(ID3D12Device5_EnumerateMetaCommands(extended, &actual, descriptions.data()), "actual meta-command descriptions");
        Check(actual <= count, "stable device meta-command enumeration");
        meta.known = true;
        for (UINT i = 0; i < actual; ++i) {
            meta.initialization_dirty |= static_cast<UINT>(descriptions[i].InitializationDirtyState);
            meta.execution_dirty |= static_cast<UINT>(descriptions[i].ExecutionDirtyState);
        }
        ReleaseCom(extended);
    }
    meta.device_identity = g_next_command_device_identity.fetch_add(1);
    Hr(ID3D12Device_SetPrivateData(fixture.device, kCommandStateMetaContract, sizeof(meta), &meta), "publish actual capability receipt");
    EnsureCommandCreationObserver(fixture.device);
    Check(EnsureNativeCommandSignatureHook(fixture.device), "real command signature observation installed");
    queue_test::List warm(fixture);
    auto snapshot = std::make_unique<NativeCommandState>();
    Check(!CaptureNativeCommandState(warm.native, *snapshot), "recording before full setter observation stays unknown");
    const auto* record = FindCommandStateTokenLocked(ReadCommandStateToken(warm.native));
    Check(record && record->hooks_ready, "first actual capture installed production native setter family");
    ReleaseNativeCommandState(*snapshot);
    warm.Close();
}

NgxResult CallNativeTestDouble(ColdScene& scene, ID3D12GraphicsCommandList* list, NgxResult status) {
    native_color_test::Params params(scene.original.resource, false);
    NgxHandle handle{711}; unsigned callback = 919;
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
    ID3D12DescriptorHeap* heap = nullptr;
    Hr(ID3D12Device_CreateDescriptorHeap(scene.fixture.device, &hd, IID_ID3D12DescriptorHeap,
                                        reinterpret_cast<void**>(&heap)), "native callback real descriptor heap");
    const auto destination = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    const auto before = native_test_double_calls;
    const auto native = [&](ID3D12GraphicsCommandList* actual_list, const NgxHandle* actual_handle,
                            const NgxParams* actual_params, void* actual_callback) {
        ++native_test_double_calls;
        Check(actual_list == list && actual_handle == &handle && actual_params == &params.base && actual_callback == &callback,
              "production native wrapper preserves all callback arguments");
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(scene.fixture.device, scene.original.resource, &view, destination);
        return status;
    };
    const auto result = NrCallNativeWithFormatObservation(native, list, &handle, &params.base, &callback, scene.generation);
    Check(result == status && native_test_double_calls == before + 1, "native testDouble runs once and returns its real scripted status");
    ReleaseCom(heap); return result;
}

struct NrScript {
    queue_test::Fixture* fixture;
    NrSlot* slot;
    post_output_test::ClearViews* clear;
    bool fail;
};
NgxResult NrEvaluateTestDouble(void*, ID3D12GraphicsCommandList* list, const void* handle,
                              const void* params, void* callback) {
    auto* script = static_cast<const NrScript*>(params);
    Check(script && handle == script->slot && callback == nullptr, "explicit NR callback seam received its original arguments");
    ++nr_test_double_calls;
    if (script->fail) return kScriptedNrFailure;
    const float neural[] = {.85f, .65f, .75f, 1.f};
    script->clear->Clear(script->fixture->device, list, script->slot->neural, 1, neural);
    return kNgxSuccess;
}
NgxResult CallNrTestDouble(queue_test::Fixture& fixture, ID3D12GraphicsCommandList* list,
                           NrSlot& slot, post_output_test::ClearViews& clear, bool fail) {
    NrScript script{&fixture, &slot, &clear, fail};
    const auto saved = g_nr_api.bridge_evaluate;
    g_nr_api.bridge_evaluate = NrEvaluateTestDouble;
    const auto result = g_nr_api.bridge_evaluate(nullptr, list, &slot, &script, nullptr);
    g_nr_api.bridge_evaluate = saved;
    // Exactly the production result adapter, not direct ledger injection.
    NrRecoveryRecord(slot.recovery_token, nr_recovery::Stage::NrEvaluate, result == kNgxSuccess, result);
    return result;
}

void ColdNativeCarrier(ColdScene& scene) {
    queue_test::List list(scene.fixture);
    {
        NrRecoveryCarrierScope carrier(list.native, scene.generation);
        ++g_carrier_evaluations;
        scene.AssertCold();
        Check(CallNativeTestDouble(scene, list.native, kNgxSuccess) == kNgxSuccess, "cold native SR status preserved");
    }
    list.Close();
    const auto details = SnapshotNrRecoveryDetails();
    Check(!details.proof.attempt_id && details.proof.state != nr_recovery::State::RecoverySucceeded,
          "cold actual native observation does not itself count as NR recovery");
}

void InitializeResultReference(ColdScene& scene) {
    auto& slot = *scene.slot;
    queue_test::List init(scene.fixture); ColorContract color{};
    Transition(init.native, slot.result, slot.result_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(init.native, slot, g_codec.copy, 8, 6, color, false, 3, 2, 0, 0,
                  slot.codec_color_mode, 8, 6, 8, 6, 1, 2);
    UavBarrier(init.native, slot.result); post_output_test::Submit(scene.fixture, init);
}

nr_recovery::Token Frame(ColdScene& scene, Fault fault, bool gate_completion) {
    auto& fixture = scene.fixture;
    queue_test::List list(fixture); nr_recovery::Token token;
    post_output_test::ClearViews clear(fixture.device);
    const auto submitted_before = scene.slot ? Read(scene.slot->recovery_token).submitted_count : 0;
    ID3D12Resource* unused_result = nullptr; ID3D12Fence* gate = nullptr;
    bool nr_ok = false, native_ok = false;
    {
        NrRecoveryCarrierScope carrier(list.native, scene.generation);
        ++g_carrier_evaluations;
        BeginPipelineStatsScopeLocked(scene.generation, 2);
        ID3D12GraphicsCommandList_SetPipelineState(list.native, g_codec.copy);
        NativeCommandStateScope state(list.native);
        Check(state.active, "actual production command snapshot captured a real observed PSO");
        NrRecoveryCommandObserved();
        scene.AdmitNextCarrier(); token = scene.slot->recovery_token;
        Check(static_cast<bool>(token), "production slot adoption starts an actual recovery attempt");
        auto& slot = *scene.slot; auto& context = queue_test::context;
        InitializeResultReference(scene);
        if (fault == Fault::WrongTarget) {
            Check(CreateTexture(fixture.device, scene.original.width, scene.original.height, L"RecoveryFixture.UnusedResult", &unused_result),
                  "wrong-target control owns a second real FP16 resource");
            auto destination = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
            destination.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * kCodecResultUavIndex;
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            ID3D12Device_CreateUnorderedAccessView(fixture.device, unused_result, nullptr, &view, destination);
        }
        ColorContract color{};
        ArmDeltaStatsCaptureLocked(slot, context, color);
        Check(RecordNrUseLocked(list.native, context, slot), "actual NR recording lifetime owns the resources");
        Transition(list.native, slot.proxy, slot.proxy_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchCodec(list.native, slot, g_codec.encode_v2, 8, 6, color, false, 3, 2, 0, 0,
                      slot.codec_color_mode, 8, 6, 8, 6, 1, 2);
        UavBarrier(list.native, slot.proxy);
        Transition(list.native, slot.proxy, slot.proxy_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list.native, slot.neural, slot.neural_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        nr_ok = CallNrTestDouble(fixture, list.native, slot, clear, fault == Fault::NrFailure) == kNgxSuccess;
        if (nr_ok) {
            ++context.evaluations; CompleteNrDiagnosticEvaluation(context, slot);
            UavBarrier(list.native, slot.neural);
            Transition(list.native, slot.neural, slot.neural_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(list.native, slot.result, slot.result_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (unused_result) { auto actual_state = D3D12_RESOURCE_STATE_COMMON;
                Transition(list.native, unused_result, actual_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }
            DispatchCodec(list.native, slot, g_codec.combine_v2, 8, 6, color, false, 3, 2, 0, 0,
                          slot.codec_color_mode, 8, 6, 8, 6, 1, 2);
            UavBarrier(list.native, slot.result);
            Check(slot.stored_result_metadata.recorded && slot.stored_result_metadata.recovery_token == token,
                  "actual stored-read recording freezes the exact runtime token");
        }
        state.Restore();
        const auto native_status = fault == Fault::NativeFailure ? kScriptedNativeFailure : kNgxSuccess;
        native_ok = CallNativeTestDouble(scene, list.native, native_status) == kNgxSuccess;
        if (fault == Fault::NativeFailure) {
            const auto calls = nr_test_double_calls;
            Check(CallNativeTestDouble(scene, list.native, kNgxSuccess) == kNgxSuccess,
                  "original-color native retry may return success");
            Check(nr_test_double_calls == calls && Read(token).first_failure.result == static_cast<std::int32_t>(kScriptedNativeFailure),
                  "original-color retry never reruns NR or erases the failed first native result");
        }
        if (nr_ok && native_ok) {
            Check(RecordVisibleTransferLocked(list.native, scene.generation, 2), "actual visible transfer binds a precise production submit receipt");
            SetBypassReason(BypassReason::None);
        }
    }
    list.Close();
    const auto before_submit = Read(token);
    Check(before_submit.submitted_count == submitted_before,
          "recording does not manufacture a new actual submission");
    if (fault == Fault::RecordOnly) {
        Check(before_submit.state != nr_recovery::State::RecoverySucceeded && before_submit.submitted_count == 0 &&
              !before_submit.writeback_verified, "record-only control has neither submitted nor stored proof");
        list.Detach();
    } else {
        if (gate_completion) {
            gate = fixture.Fence(); Hr(ID3D12CommandQueue_Wait(fixture.a, gate, 1), "hold real completion before collector");
        }
        list.Submit(fixture.a);
        if (gate_completion && nr_ok && native_ok) {
            CollectSlotDeltaStatsLocked(queue_test::context, *scene.slot);
            Check(scene.slot->delta_stats_pending && !Read(token).writeback_verified,
                  "submitted but unfinished actual GPU work cannot publish a stored proof");
            Hr(ID3D12Fence_Signal(gate, 1), "release actual fixture GPU dependency");
        }
        fixture.Drain(fixture.a);
        if (nr_ok) {
            CollectSlotDeltaStatsLocked(queue_test::context, *scene.slot);
            Check(scene.slot->delta_stats_pending, "even completed GPU work waits for its retained recording owner");
        }
        list.Detach();
        CollectSlotDeltaStatsLocked(queue_test::context, *scene.slot);
        if (nr_ok) {
            const auto stored = SnapshotPreStoreEvidence();
            Check(!scene.slot->delta_stats_pending && stored.recovery_token == token && stored.samples != 0,
                  "actual collector publishes only the completed frame's frozen stored evidence");
            Check(stored.recovery_target == scene.slot->recovery_target, "stored target identity agrees with the frame's bound owned Result");
        }
    }
    ReleaseCom(gate); ReleaseCom(unused_result);
    return token;
}

void GenuineBypass(ColdScene& scene) {
    queue_test::List list(scene.fixture);
    {
        NrRecoveryCarrierScope carrier(list.native, scene.generation);
        ++g_carrier_evaluations;
        scene.Resolve(); // Classified/admitted semantics, deliberately no NR use.
        const auto before = nr_test_double_calls;
        CallNativeTestDouble(scene, list.native, kNgxSuccess);
        Check(nr_test_double_calls == before, "genuine native-only carrier did not run the NR callback");
    }
    list.Close();
}

void Case(queue_test::Fixture& fixture, const Scenario& scenario) {
    const auto nr_before = nr_test_double_calls, native_before = native_test_double_calls;
    ColdScene scene(fixture); ColdNativeCarrier(scene);
    if (scenario.fault == Fault::MissingDescriptor) {
        // Physical descriptors are real and safe to execute. Deliberately drop
        // their receipt before the first attempt to exercise missing evidence;
        // AdoptSlot must not reconstruct it from nonnull pointers/format fields.
        auto& slot = queue_test::context.slots[0];
        Check(CreateSlot(fixture.device, scene.original.resource, 8, 6, false, nullptr, slot,
                         CodecColorMode::ModelEncodedValueDomain, 0, scene.generation), "real preexisting descriptor set for missing-receipt control");
        slot.descriptor_receipt = {};
    }
    nr_recovery::Token token;
    const unsigned count = scenario.fault == Fault::None || scenario.fault == Fault::MissingDescriptor ? 3u : 1u;
    for (unsigned i = 0; i < count; ++i) {
        token = Frame(scene, scenario.fault, scenario.fault == Fault::None && i == 0);
        const auto proof = Read(token);
        if (scenario.fault == Fault::None) {
            Check(proof.was_waiting, "native-view signature transition retains its cold waiting history");
            Check(proof.state == (i < 2 ? nr_recovery::State::Pending : nr_recovery::State::RecoverySucceeded),
                  "runtime recovers only after three actual consecutive carriers and a matching stored proof");
        } else Check(proof.state != nr_recovery::State::RecoverySucceeded, "runtime fault never publishes Succeeded");
    }
    auto proof = Read(token);
    if (scenario.fault == Fault::None) {
        const auto input = SnapshotNrCompatibilityVerdict();
        const auto verdict = nr_compatibility_verdict::Resolve(input);
        std::printf("RUNTIME_RECOVERY_SNAPSHOT config=%llu feature=%llu requested=%u effective=%u resource=%d/%d/%d resource_reason=%.*s resource_missing=%.*s block=%.*s stage=%s reason=%s next=%s decision_evidence=%d visual=%u\n",
                    static_cast<unsigned long long>(input.current.config), static_cast<unsigned long long>(input.current.feature_generation),
                    input.current.requested_mode, input.current.effective_mode,
                    input.resource.available, input.resource.admitted, input.resource.definitive,
                    static_cast<int>(input.resource.reason.size()), input.resource.reason.data(),
                    static_cast<int>(input.resource.missing_evidence.size()), input.resource.missing_evidence.data(),
                    static_cast<int>(input.first_block.reason.size()), input.first_block.reason.data(),
                    nr_compatibility_verdict::Name(verdict.stage), verdict.reason.c_str(),
                    nr_compatibility_verdict::Name(verdict.nextAction), verdict.evidenceComplete, static_cast<unsigned>(input.visual.state));
        Check(input.recovery.state == nr_compatibility_verdict::RecoveryState::Succeeded &&
              verdict.nextAction == nr_compatibility_verdict::NextAction::NeedsVisualF7 &&
              verdict.stage == nr_compatibility_verdict::Stage::Visual &&
              verdict.reason == "execution-recovered-final-visual-unverified" &&
              input.visual.state == nr_compatibility_verdict::FactState::Unobserved,
              "technical recovery requires a separate final visual F7 proof");
        const auto details = SnapshotNrRecoveryDetails();
        Check(details.proof.proof_attempt_id == token.attempt_id && details.proof.correlated_window,
              "runtime detail snapshot uses the exact current attempt and completed carrier window");
        GenuineBypass(scene);
        Check(Read(token).state == nr_recovery::State::Pending && !Read(token).correlated_window,
              "a real subsequent NR bypass breaks recovery continuity");
        g_config_generation.fetch_add(1);
        const auto changed = SnapshotNrCompatibilityVerdict();
        Check(!changed.recovery.attempt_id && changed.recovery.state != nr_compatibility_verdict::RecoveryState::Succeeded,
              "new config generation cannot inherit the old attempt or actual proof");
    } else if (scenario.fault == Fault::MissingDescriptor) {
        Check(!proof.descriptor_verified && !proof.correlated_window && !proof.writeback_verified,
              "three real writes/submits cannot replace the missing descriptor-programming receipt");
    } else if (scenario.fault == Fault::NrFailure || scenario.fault == Fault::NativeFailure) {
        const auto expected = scenario.fault == Fault::NrFailure ? kScriptedNrFailure : kScriptedNativeFailure;
        Check(proof.state == nr_recovery::State::Latched && proof.first_failure.result == static_cast<std::int32_t>(expected),
              "actual returned callback failure is latched with its original code");
        const auto before = nr_test_double_calls;
        GenuineBypass(scene);
        Check(nr_test_double_calls == before && Read(token).state == nr_recovery::State::Latched,
              "later native success cannot erase or blindly retry a failed interpretation");
    } else if (scenario.fault == Fault::WrongTarget) {
        Check(proof.state == nr_recovery::State::Latched && proof.first_failure.stage == nr_recovery::Stage::ActualStorageRead,
              "real wrong-target GPU store is rejected by the actual bound-Result readback");
    }
    std::printf("RUNTIME_RECOVERY_CASE %s PASS nr_callbacks=%u native_callbacks=%u state=%s attempt=%llu proof=%llu\n",
                scenario.name, nr_test_double_calls - nr_before, native_test_double_calls - native_before,
                nr_recovery::StateName(proof.state), static_cast<unsigned long long>(proof.attempt_id),
                static_cast<unsigned long long>(proof.proof_attempt_id));
    scene.RetireAfterDrainAndCollection(scenario.fault != Fault::RecordOnly);
}

int Run(int argc, char** argv) {
    Check(GetEnvironmentVariableA("NR_VALIDATION_WARP_ONLY", nullptr, 0) != 0,
          "runner explicitly selected WARP-only validation");
    queue_test::Fixture fixture;
    Check(EnsureCodec(fixture.device) && g_codec.post_store_probe, "actual complete production codec and stored probe compiled");
    InstallCommandObservation(fixture);
    const char* selected = argc == 2 ? argv[1] : "all";
    unsigned executed = 0;
    for (const auto& scenario : scenarios) if (!std::strcmp(selected, "all") || !std::strcmp(selected, scenario.name)) {
        Case(fixture, scenario); ++executed;
    }
    Check(executed != 0, "requested runtime scenario exists");
    std::printf("RUNTIME_RECOVERY WARP PASS cases=%u checks=%u; real full Core/descriptor/command/Execute/fence/readback; scripted NR/native callback results, no NVIDIA NR/game\n",
                executed, queue_test::checks);
    return 0;
}
} // namespace resource_recovery_runtime_test
