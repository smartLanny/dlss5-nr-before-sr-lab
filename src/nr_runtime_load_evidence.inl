// Included before EnsureNrApi; values are metadata, never permission to bypass a contract.
#define NR_HAS_RUNTIME_LOAD_EVIDENCE 1
struct RuntimeLoadEvidence {
    bool available = false;
    bool loaded[3] = {};
    DWORD errors[3] = {};
    unsigned missing_exports = 0;
    unsigned init_result = 0;
    const char *stage = "not-attempted";
    ULONGLONG sampled_at = 0;
};
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
// The caller invokes this immediately after each API. Later GetLastError calls
// must never erase an earlier dependency failure (#97).
DWORD RuntimeLoadError(bool succeeded) {
    if (succeeded) return ERROR_SUCCESS;
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}
