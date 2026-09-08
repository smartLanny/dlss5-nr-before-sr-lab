#!/usr/bin/env python3
"""Freeze native-carrier completion: real GPU proof, no caught NGX faults.

Applied after the pinned carrier production-lifetime transform and before build.
Waiting before DX11 copyback costs a CPU/GPU synchronization point per frame.
Timeout permanently disables this carrier and retains unproven dependencies.
The production activation lock serializes ordinary retirement; timeout quarantine
still deliberately keeps uncertain GPU resources until process exit.
"""
from pathlib import Path
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('carrier_completion_parser',ROOT/'scripts/patch-format-access.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
once,edit=base.once,base.edit_block

HELPERS='''// Completion is only a GPU fence observation, never a CPU Signal.
static bool g_nr_carrier_quarantined = false; // protected by g_bridge_cs
typedef NVSDK_NGX_Result (*PFN_DestroyParameters)(NVSDK_NGX_Parameter *);
typedef NVSDK_NGX_Result (*PFN_Shutdown1)(ID3D12Device *);
static PFN_DestroyParameters g_nr_destroy_parameters = nullptr;
static PFN_Shutdown1 g_nr_shutdown1 = nullptr;
static bool g_nr_ngx_initialized = false;
static NVSDK_NGX_Parameter *g_nr_capability_parameters = nullptr;
static bool BridgeReleaseSession();
static void NrCarrierQuarantine(const char *why) {
    g_nr_carrier_quarantined = true;
    g_bridge.disabled = true;
    g_bridge.frame_ready = false;
    g_nr_native_delivery = false;
    Log("[native-carrier] stopped and retained unproven GPU dependencies: %s; native game path retained, restart required.", why);
}
static bool NrWaitForGpu(ID3D12Fence *fence, UINT64 value, DWORD timeout_ms) {
    if (!value) return true;
    if (!fence) return false;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const UINT64 done = fence->GetCompletedValue();
        if (done == UINT64_MAX) return false;
        if (done >= value) return true;
        if (GetTickCount64() - start >= timeout_ms) return false;
        Sleep(1); // bounded CPU wait; no synthetic completion or stale event
    }
}

'''

DRAIN='''static bool BridgeDrainGpu()
{
    if (g_nr_carrier_quarantined) return false;
    if (!g_bridge.queue) return true; // nothing can have been submitted
    if (!g_bridge.fence12 || !g_bridge.fence_progress) {
        NrCarrierQuarantine("missing retirement fence"); return false;
    }
    const UINT64 value = ++g_bridge.fence_value;
    // Retire DX11 reads/copies as well as D3D12 writes before freeing mirrors.
    if (g_bridge.ctx4 && g_bridge.fence11) {
        if (FAILED(g_bridge.ctx4->Signal(g_bridge.fence11, value))) {
            NrCarrierQuarantine("DX11 retirement signal failed"); return false;
        }
        g_bridge.ctx4->Flush();
        if (FAILED(g_bridge.queue->Wait(g_bridge.fence12, value))) {
            NrCarrierQuarantine("DX12 retirement wait failed"); return false;
        }
    }
    if (FAILED(g_bridge.queue->Signal(g_bridge.fence_progress, value)) ||
        !NrWaitForGpu(g_bridge.fence_progress, value, 5000)) {
        NrCarrierQuarantine("GPU retirement was not completed"); return false;
    }
    for (int i = 0; i < Bridge::kFrames; ++i) g_bridge.alloc_fence[i] = 0;
    return true;
}'''

SESSION='''static bool BridgeReleaseSession()
{
    if (g_nr_carrier_quarantined) return false;
    BridgeReleaseTextures();
    if (g_nr_carrier_quarantined) return false;

    if (g_bridge.params != nullptr) {
        if (g_nr_destroy_parameters == nullptr ||
            g_nr_destroy_parameters(g_bridge.params) != NGX_SUCCESS) {
            NrCarrierQuarantine("NGX parameter destruction failed");
            return false;
        }
        g_bridge.params = nullptr;
    }
    if (g_nr_capability_parameters != nullptr) {
        if (g_nr_destroy_parameters == nullptr ||
            g_nr_destroy_parameters(g_nr_capability_parameters) != NGX_SUCCESS) {
            NrCarrierQuarantine("NGX capability parameter destruction failed");
            return false;
        }
        g_nr_capability_parameters = nullptr;
    }
    if (g_nr_ngx_initialized && g_bridge.dev12 != nullptr) {
        if (g_nr_shutdown1 == nullptr || g_nr_shutdown1(g_bridge.dev12) != NGX_SUCCESS) {
            NrCarrierQuarantine("NGX D3D12 shutdown failed");
            return false;
        }
    }

    if (g_bridge.mt != nullptr) { g_bridge.mt->Release(); g_bridge.mt = nullptr; }
    if (g_bridge.ctx4 != nullptr) { g_bridge.ctx4->Release(); g_bridge.ctx4 = nullptr; }
    if (g_bridge.fence11 != nullptr) { g_bridge.fence11->Release(); g_bridge.fence11 = nullptr; }
    if (g_bridge.fence12 != nullptr) { g_bridge.fence12->Release(); g_bridge.fence12 = nullptr; }
    if (g_bridge.fence_progress != nullptr) { g_bridge.fence_progress->Release(); g_bridge.fence_progress = nullptr; }
    if (g_bridge.stall_fence != nullptr) { g_bridge.stall_fence->Release(); g_bridge.stall_fence = nullptr; }
    if (g_bridge.list != nullptr) { g_bridge.list->Release(); g_bridge.list = nullptr; }
    for (int i = 0; i < Bridge::kFrames; ++i) {
        if (g_bridge.alloc[i] != nullptr) { g_bridge.alloc[i]->Release(); g_bridge.alloc[i] = nullptr; }
        g_bridge.alloc_fence[i] = 0;
    }
    if (g_bridge.queue != nullptr) { g_bridge.queue->Release(); g_bridge.queue = nullptr; }
    if (g_bridge.adapter3 != nullptr) { g_bridge.adapter3->Release(); g_bridge.adapter3 = nullptr; }
    if (g_bridge.dev12 != nullptr) { g_bridge.dev12->Release(); g_bridge.dev12 = nullptr; }
    if (g_bridge.fence_event != nullptr) { CloseHandle(g_bridge.fence_event); g_bridge.fence_event = nullptr; }

    g_bridge.session_ready = false;
    g_bridge.frame_ready = false;
    g_bridge.pending_out = 0;
    g_bridge.fence_value = 0;
    g_bridge.last_submitted = 0;
    g_bridge.frame_slot = 0;
    g_bridge.disabled = false;
    g_nr_ngx_initialized = false;
    g_bridge.alloc_params = nullptr;
    g_bridge.create_feature = nullptr;
    g_bridge.eval_feature = nullptr;
    g_bridge.release_feature = nullptr;
    g_nr_destroy_parameters = nullptr;
    g_nr_shutdown1 = nullptr;
    return true;
}'''

END='''static UINT64 EndCommands()
{
    if (g_nr_carrier_quarantined || FAILED(g_bridge.list->Close())) {
        NrCarrierQuarantine("command list Close failed"); return 0;
    }
    ID3D12CommandList *lists[] = { g_bridge.list };
    g_bridge.queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++g_bridge.fence_value;
    // Immediately pin the allocator even if publication of completion fails.
    g_bridge.alloc_fence[g_bridge.frame_slot] = UINT64_MAX;
    if (!g_bridge.fence_progress ||
        FAILED(g_bridge.queue->Signal(g_bridge.fence_progress, value))) {
        NrCarrierQuarantine("submission progress signal failed"); return 0;
    }
    g_bridge.last_submitted = value;
    g_bridge.alloc_fence[g_bridge.frame_slot] = value;
    g_bridge.frame_slot = (g_bridge.frame_slot + 1) % Bridge::kFrames;
    return value;
}'''

def remove_fault_conversion(text, signature):
    def patch(s):
        start=s.index('    __try')
        stop=s.index('    __except',start)
        guarded=s[start:stop].replace('    __try ', '',1).strip()
        # Keep the body and ordinary return codes; hardware faults propagate.
        return s[:start]+'    '+guarded+'\n}'
    return edit(text,signature,patch)

def patch_host(text):
    for signature in ('static NVSDK_NGX_Result SafeInitExt(',
                      'static NVSDK_NGX_Result SafeInitProjectID(',
                      'static NVSDK_NGX_Result SafeAllocParams(',
                      'static NVSDK_NGX_Result SafeGetCaps(',
                      'static NVSDK_NGX_Result SafeCreateFeature('):
        text=remove_fault_conversion(text,signature)
    return text

def patch_bridge(text):
    text=remove_fault_conversion(text,'static NVSDK_NGX_Result SafeEvaluateGuarded(')
    text=edit(text,'static HRESULT SafeCreateDevice(',lambda s:once(s,
        '''        __try { hr = fn(ad, kLevels[i], __uuidof(ID3D12Device),
                        reinterpret_cast<void **>(out)); }
        __except (CaptureFault(GetExceptionInformation()))
        { *code = GetExceptionCode(); return E_FAIL; }''',
        '''        hr = fn(ad, kLevels[i], __uuidof(ID3D12Device),
                reinterpret_cast<void **>(out));'''))
    text=once(text,'static void BridgeDisable(const char *why)',HELPERS+'static void BridgeDisable(const char *why)')
    def disable(s):
        a=s.index('    // Before anything else.')
        b=s.index('    BridgeReleaseTextures();',a)
        return s[:a]+s[b:]
    text=edit(text,'static void BridgeDisable(',disable)
    text=edit(text,'static void BridgeDrainGpu()',lambda s:DRAIN)
    def release(s):
        s=once(s,'    PqPassRelease(&g_bridge.pq);\n    BridgeDrainGpu();',
            '    if (!BridgeDrainGpu()) return;\n'
            '    if (g_bridge.feature) {\n'
            '        if (!g_bridge.release_feature || g_bridge.release_feature(g_bridge.feature) != NGX_SUCCESS) {\n'
            '            NrCarrierQuarantine("NGX feature Release failed"); return;\n'
            '        }\n'
            '        g_bridge.feature = nullptr;\n'
            '    }\n'
            '    PqPassRelease(&g_bridge.pq);\n'
            '    if (g_nr_carrier_quarantined) return;')
        return once(s,'''    if (g_bridge.feature != nullptr && g_bridge.release_feature != nullptr)
    {
        g_bridge.release_feature(g_bridge.feature);
        g_bridge.feature = nullptr;
    }
''','')
    text=edit(text,'static void BridgeReleaseTextures()\n{',release)
    text=edit(text,'static void BridgeReleaseTextures()\n{',lambda s:s+'\n\n'+SESSION)
    text=once(text,'''        g_bridge.release_feature = reinterpret_cast<PFN_D3D12ReleaseFeature>(
            GetProcAddress(ngx, "NVSDK_NGX_D3D12_ReleaseFeature"));''','''        g_bridge.release_feature = reinterpret_cast<PFN_D3D12ReleaseFeature>(
            GetProcAddress(ngx, "NVSDK_NGX_D3D12_ReleaseFeature"));
        g_nr_destroy_parameters = reinterpret_cast<PFN_DestroyParameters>(
            GetProcAddress(ngx, "NVSDK_NGX_D3D12_DestroyParameters"));
        g_nr_shutdown1 = reinterpret_cast<PFN_Shutdown1>(
            GetProcAddress(ngx, "NVSDK_NGX_D3D12_Shutdown1"));''')
    text=once(text,'''        if (init_ext == nullptr || g_bridge.alloc_params == nullptr ||
            g_bridge.create_feature == nullptr || g_bridge.eval_feature == nullptr)
        { Log("[bridge] required NGX exports missing"); goto fail; }''','''        if (init_ext == nullptr || g_bridge.alloc_params == nullptr ||
            g_bridge.create_feature == nullptr || g_bridge.eval_feature == nullptr ||
            g_bridge.release_feature == nullptr || g_nr_destroy_parameters == nullptr ||
            g_nr_shutdown1 == nullptr)
        { Log("[bridge] required NGX lifecycle exports missing"); goto fail; }''')
    text=once(text,'        Log("[bridge] NGX D3D12 session initialised");',
        '        g_nr_ngx_initialized = true;\n        Log("[bridge] NGX D3D12 session initialised");')
    text=once(text,'            NVSDK_NGX_Result rc = SafeGetCaps(get_caps, &caps, &code);',
        '            NVSDK_NGX_Result rc = SafeGetCaps(get_caps, &caps, &code);\n'
        '            if (caps != nullptr) g_nr_capability_parameters = caps;')
    text=edit(text,'static void BridgeDisable(',lambda s:once(s,
        '''    BridgeReleaseTextures();
    if (g_bridge.adapter3 != nullptr)
    { g_bridge.adapter3->Release(); g_bridge.adapter3 = nullptr; }''',
        '    BridgeReleaseSession();\n    g_bridge.disabled = true;'))
    text=once(text,'    BridgeReleaseExposureFwd();',
        '    BridgeReleaseExposureFwd();\n    if (g_nr_carrier_quarantined) return;')
    text=edit(text,'static bool BridgeBuildResources(',lambda s:once(s,
        '    BridgeReleaseTextures();','    BridgeReleaseTextures();\n    if (g_nr_carrier_quarantined) return false;'))
    text=edit(text,'static void BridgeReleaseExposure()',lambda s:once(s,
        '        BridgeDrainGpu();','        if (!BridgeDrainGpu()) return;'))
    text=edit(text,'static bool BridgeEnsureExposure(',lambda s:once(s,
        '    BridgeReleaseExposure();\n\n    D3D11_TEXTURE2D_DESC td',
        '    BridgeReleaseExposure();\n    if (g_nr_carrier_quarantined) return false;\n\n    D3D11_TEXTURE2D_DESC td'))
    text=edit(text,'static void BridgeEnterStall(',lambda s:
        s[:s.index('{')]+'''{
    (void)done; (void)wanted; (void)who;
    NrCarrierQuarantine("GPU progress timeout; no CPU fence release");
}''')
    text=edit(text,'static void BridgeRescueStalledQueue()',lambda s:
        s[:s.index('{')]+'''{
    // This candidate never queues a game-side GPU wait for incomplete NR.
    // Per-frame CPU completion and retirement checks own failure handling.
}''')
    text=edit(text,'static UINT64 EndCommands()\n{',lambda s:END)
    text=edit(text,'static bool PqPassWait(',lambda s:s[:s.index('{')]+'''{
    return NrWaitForGpu(p->fence, value, wait_ms);
}''')
    text=edit(text,'static void PqPassRelease(',lambda s:once(s,
        '    if (p->fence != nullptr) PqPassWait(p, p->fv);',
        '    if (g_nr_carrier_quarantined) return;\n'
        '    if (!PqPassWait(p, p->fv)) { NrCarrierQuarantine("PQ retirement incomplete"); return; }'))
    text=edit(text,'static bool PqPassMake(',lambda s:once(s,
        '    PqPassRelease(p);\n    p->dev',
        '    PqPassRelease(p);\n    if (g_nr_carrier_quarantined) return false;\n    p->dev'))
    # A zero submission token is failure, never proof that feature creation ran.
    text=once(text,'    const UINT64 v = EndCommands();\n\n    // Feature creation',
        '    const UINT64 v = EndCommands();\n'
        '    if (!v || !NrWaitForGpu(g_bridge.fence_progress, v, 5000)) {\n'
        '        NrCarrierQuarantine("feature creation completion unavailable"); return false;\n'
        '    }\n\n    // Feature creation')
    # The old post-create check used the shared fence; it is not a completion fence.
    a=text.index('    // Feature creation is a one-off')
    b=text.index('    if (code != 0)',a)
    text=text[:a]+text[b:]
    text=once(text,'    g_bridge.ctx4->Signal(g_bridge.fence11, v_in);',
        '    if (FAILED(g_bridge.ctx4->Signal(g_bridge.fence11, v_in))) {\n'
        '        NrCarrierQuarantine("DX11 input signal failed");\n'
        '        for (int i=0;i<SLOT_COUNT;++i) g[i]->Release(); dev11->Release(); return;\n'
        '    }')
    text=once(text,'    g_bridge.queue->Wait(g_bridge.fence12, v_in);',
        '    if (FAILED(g_bridge.queue->Wait(g_bridge.fence12, v_in))) {\n'
        '        NrCarrierQuarantine("DX12 input wait failed");\n'
        '        for (int i=0;i<SLOT_COUNT;++i) g[i]->Release(); dev11->Release(); return;\n'
        '    }')
    text=once(text,'    UINT64 v_out = EndCommands();',
        '    UINT64 v_out = EndCommands();\n'
        '    if (!v_out || !NrWaitForGpu(g_bridge.fence_progress, v_out, 500)) {\n'
        '        NrCarrierQuarantine("frame completion unavailable; native image retained");\n'
        '        for (int i=0;i<SLOT_COUNT;++i) g[i]->Release(); dev11->Release(); return;\n'
        '    }')
    text=once(text,'        g_bridge.ctx4->Wait(g_bridge.fence11, v_out);',
        '        // Real D3D12 progress was observed before reaching copyback.')
    text=once(text,'        g_bridge.pending_out = v_out;',
        '        g_bridge.pending_out = 0; // no cancellable or CPU-released game wait')
    # The synthetic/PQ path is disabled in this native-only carrier; reject it
    # explicitly rather than accidentally treating its separate queue as done.
    text=once(text,'    if (g_bridge.pq.in != nullptr)\n        v_out = PqPassAfter(&g_bridge.pq, g_bridge.queue, g_bridge.fence12, &g_bridge.fence_value, v_out);',
        '    if (g_bridge.pq.in != nullptr) {\n'
        '        NrCarrierQuarantine("unexpected synthetic PQ route in native carrier");\n'
        '        for (int i=0;i<SLOT_COUNT;++i) g[i]->Release(); dev11->Release(); return;\n'
        '    }')
    # Remove inherited explanations of the now-removed asynchronous rescue.
    for start,stop,replacement in (
        ('    // No CPU wait here, and that is deliberate', '    // One readback, one frame, once per session',
         '    // This candidate waits on actual GPU progress before any DX11 copyback.\n'
         '    // The CPU synchronization has a frame-time cost; no performance claim.\n\n'),
        ('// Frame N makes the game\'s queue wait', 'static void LogDred()',
         '// Device-loss diagnosis remains available; it never fabricates completion.\n'),
        ('// Release the game and stand aside, rather than stop for good.', 'static void BridgeEnterStall(',
         '// Progress failure disables once and keeps unproven dependencies alive.\n'),
        ('        // 4. take the result back into the game\'s own output texture.',
         '        const UINT64 n = ++g_bridge.frames_done;',
         '        // Real progress has completed; no DX11 GPU wait needs rescuing.\n'
         '        g_bridge.pending_out = 0;\n\n')):
        if text.count(start)!=1 or text.count(stop)!=1:raise ValueError('Carrier completion comment anchors changed')
        a=text.index(start);b=text.index(stop,a);text=text[:a]+replacement+text[b:]
    return text

def apply(stage):
    stage=Path(stage)
    bridge=stage/'carrier/src/bridge.inc';host=stage/'carrier/src/dlss5-bridge.cpp'
    outputs=((bridge,patch_bridge(bridge.read_text(encoding='utf-8'))),
             (host,patch_host(host.read_text(encoding='utf-8'))))
    for path,text in outputs:path.write_text(text,encoding='utf-8',newline='\n')
