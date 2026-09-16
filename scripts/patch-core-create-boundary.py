"""D22 Core entry coverage: compose after the existing D21 source adapters."""

def once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f'Core-entry anchor changed ({count}): {old[:100]!r}')
    return text.replace(old, new, 1)


def patch_core(text: str) -> str:
    # force is used at cold API/module boundaries only. A statically linked NGX
    # client may load _nvngx without any nvngx.dll loader Init being observable.
    old = '''    if (!g_evaluate_hooks_ready.load(std::memory_order_acquire) &&
        !g_loader_init_hooks_ready.load(std::memory_order_acquire))
        return;'''
    new = '''    if (!g_evaluate_hooks_ready.load(std::memory_order_acquire) &&
        !g_loader_init_hooks_ready.load(std::memory_order_acquire) &&
        !(force && GetModuleHandleW(L"_nvngx.dll") != nullptr))
        return;'''
    text = once(text, old, new)
    text = once(text, 'DWORD WINAPI HookWorker(void *) {',
        '#include "nr_core_create_boundary.inl"\n\nDWORD WINAPI HookWorker(void *) {')
    text = once(text, '''    for (unsigned int attempt = 0; attempt < 24000; ++attempt) {
        if (g_evaluate_hooks_ready.load(std::memory_order_acquire)) return 0;''',
        '''    // Cover the SDK's resolved native Create boundary before the model
    // wrapper exists. Never install a graphics/API hook inside GetProcAddress.
    (void)InstallCoreCreateBoundaryHook();
    for (unsigned int attempt = 0; attempt < 24000; ++attempt) {
        if (g_evaluate_hooks_ready.load(std::memory_order_acquire)) return 0;
        if (!g_core_lifecycle_hooks_ready.load(std::memory_order_acquire)) {
            LoadedNgxModule early_core(L"_nvngx.dll");
            if (early_core.handle != nullptr) PollNgxLifecycleModules(true);
        }''')
    return text


def patch_feedback(text: str) -> str:
    signature = "void FeedbackAppendActivity(FeedbackSession &s, const char *prefix) {"
    return once(text, signature, signature + '\n#include "nr_core_create_boundary_feedback.inl"')
