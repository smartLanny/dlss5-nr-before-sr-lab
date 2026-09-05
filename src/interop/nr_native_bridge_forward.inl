    // Opt-in integration: retain the existing native hook/parameter ABI, but
    // use this call's queued CopyResource as evidence, never last-frame health.
    EnterCriticalSection(&g_bridge_cs);
    g_eval_handle = handle;
    const auto history_key = reinterpret_cast<std::uintptr_t>(handle);
    int original_reset = 0;
    const bool reset_known = p->Get("Reset", &original_reset) == NGX_SUCCESS &&
                             (original_reset == 0 || original_reset == 1);
    const bool consumer_on = nr_native_bridge_client::AllowsFrame(false);
    const bool fast = consumer_on && g_nr_native_history.Admit(history_key, reset_known) &&
                      BridgeWillDeliver();
    auto native = [&]() -> NVSDK_NGX_Result {
        if (g_ngx_cs_ready) EnterCriticalSection(&g_ngx_cs);
        EnterCriticalSection(&g_hook_cs);
        auto* writable = const_cast<NVSDK_NGX_Parameter*>(p);
        const bool reset = reset_known && g_nr_native_history.NeedsReset(history_key);
        if (reset) writable->Set("Reset", 1);
        HookRemove(h);
        const auto result = reinterpret_cast<PFN_Evaluate>(h.target)(ctx, handle, p, cb);
        HookRestore(h);
        if (reset) writable->Set("Reset", original_reset);
        LeaveCriticalSection(&g_hook_cs);
        if (g_ngx_cs_ready) LeaveCriticalSection(&g_ngx_cs);
        g_nr_native_history.NativeCompleted(history_key, result == NGX_SUCCESS, reset_known);
        return result;
    };
    NVSDK_NGX_Result r = NGX_SUCCESS;
    // Bootstrap, partial outputs and skip_game=0 preserve original-first order.
    if (!fast) r = native();
    if (prev_source == SRC_SYNTH) {
        g_bridge.frame_ready = false;
        g_bridge.need_reset = true;
    }
    // Opening a private device is one-off bootstrap work. Per-frame copies and
    // evaluation are separately gated by the consumer's actual master switch.
    if (g_cfg.stage >= 1 && InterlockedCompareExchange(&g_probe_done, 1, 0) == 0) {
        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        if (dev) { BridgeInitSession(dev, ctx); dev->Release(); }
    }
    g_nr_native_delivery = false;
    if (r == NGX_SUCCESS) BridgeFrame(ctx, p);
    if (fast) {
        if (g_nr_native_delivery && !g_bridge.partial_output) {
            g_nr_native_history.Mirrored(history_key);
        } else {
            // Includes first-use failures, disabled NR, missing guides and
            // resolution transitions. Reset native history after any real gap.
            r = native();
        }
    }
    LeaveCriticalSection(&g_bridge_cs);
    return r;
}

