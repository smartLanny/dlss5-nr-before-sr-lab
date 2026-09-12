// Included before EnsureNrApi; values are metadata, never permission to bypass a contract.
#define NR_HAS_RUNTIME_LOAD_EVIDENCE 1
struct RuntimeLoadEvidence {
    bool available = false;
    bool loaded[3] = {};
    bool present[3] = {};
    DWORD errors[3] = {};
    unsigned classes[3] = {};
    unsigned missing_exports = 0;
    unsigned init_result = 0;
    bool bootstrap_attempted = false;
    bool bootstrap_succeeded = false;
    unsigned bootstrap_result = 0;
    const char *stage = "not-attempted";
    const char *search = "addon-directory";
    ULONGLONG sampled_at = 0;
};
unsigned RuntimeClassifyLoad(bool present, bool loaded, unsigned error) {
    if (loaded) return 1;
    if (!present) return 2;
    if (error == 126) return 3;
    return 4;
}
const char *RuntimeLoadClassName(unsigned cls) {
    switch (cls) {
    case 1: return "loaded";
    case 2: return "file-absent";
    case 3: return "dependency-missing";
    case 4: return "other-failure";
    default: return "not-attempted";
    }
}
// The caller invokes this immediately after each API. Later GetLastError calls
// must never erase an earlier dependency failure (#97).
DWORD RuntimeLoadError(bool succeeded) {
    if (succeeded) return ERROR_SUCCESS;
    const DWORD error = GetLastError();
    // Backport #119: zero is unknown, not evidence that a DLL was missing.
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}
bool RuntimeRegularFile(const wchar_t *path) {
    if (path == nullptr || *path == L'\0') return false;
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
bool RuntimeLoadRestricted(const wchar_t *path, HMODULE *module, DWORD *error) {
    constexpr DWORD kRestrictedLoad =
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    *module = LoadLibraryExW(path, nullptr, kRestrictedLoad);
    *error = RuntimeLoadError(*module != nullptr);
    return *module != nullptr;
}
SRWLOCK g_runtime_load_evidence_lock = SRWLOCK_INIT;
RuntimeLoadEvidence g_runtime_load_evidence;
void RecordRuntimeLoadEvidence(RuntimeLoadEvidence value) {
    value.available = true; value.sampled_at = GetTickCount64();
    AcquireSRWLockExclusive(&g_runtime_load_evidence_lock);
    g_runtime_load_evidence = value;
    ReleaseSRWLockExclusive(&g_runtime_load_evidence_lock);
}
RuntimeLoadEvidence SnapshotRuntimeLoadEvidence() {
    RuntimeLoadEvidence value;
    if (!TryAcquireSRWLockShared(&g_runtime_load_evidence_lock)) {
        value.stage = "snapshot-busy"; return value;
    }
    value = g_runtime_load_evidence;
    ReleaseSRWLockShared(&g_runtime_load_evidence_lock);
    return value;
}
