#include "nr_config_policy.h"

struct ConfigPolicyMigrationStatus {
    bool visible = false;
    bool jitter_changed = false;
    bool persisted = false;
    bool failed = false;
    unsigned long error = ERROR_SUCCESS;
};

// Protected by g_chain_cs. Reuse D8's policy identity without its independent
// strength migration or any change to native SR/post-SR guide contracts.
ConfigPolicyMigrationStatus g_config_policy_migration;
bool g_config_policy_attempted = false;

ConfigPolicyMigrationStatus SnapshotConfigPolicyMigrationStatus() {
    EnterCriticalSection(&g_chain_cs);
    const auto result = g_config_policy_migration;
    LeaveCriticalSection(&g_chain_cs);
    return result;
}

void ApplyConfigPolicyMigrations(ChainConfig &next, bool file_proven_absent) {
    // Even after a failed disk write, later polling must not repeat the
    // migration over this session's explicit manual choice.
    if (g_config_policy_attempted) return;
    g_config_policy_attempted = true;
    const int policy_version = static_cast<int>(GetPrivateProfileIntW(
        L"NRBeforeSR", L"JitterPolicyVersion", 0, g_ini_path));
    const bool pending = policy_version < nr_config_policy::kJitterVersion;
    auto &status = g_config_policy_migration;
    status.visible = !file_proven_absent && pending;
    if (pending) {
        status.jitter_changed = next.jitter_mode != NrJitterMode::Legacy;
        next.jitter_mode = NrJitterMode::Legacy;
    }

    // Respect a prior failed configuration write. The live default is still
    // off, but a partially written file must never be certified as migrated.
    bool ok = !g_config_persistence_failed.load(std::memory_order_acquire);
    unsigned long first_error = ok ? ERROR_SUCCESS :
        g_config_persistence_error.load(std::memory_order_relaxed);
    const auto write = [&](bool succeeded) {
        if (!succeeded && ok) {
            first_error = GetLastError();
            if (!first_error) first_error = ERROR_WRITE_FAULT;
        }
        ok = succeeded && ok;
    };
    if (pending && ok) {
        SetLastError(ERROR_SUCCESS);
        write(WriteIniInt(L"JitterMode", 0));
        if (ok) write(WriteIniInt(L"JitterPolicyVersion",
                                 nr_config_policy::kJitterVersion));
    }
    status.persisted = ok;
    status.failed = !ok;
    status.error = ok ? ERROR_SUCCESS :
        (first_error ? first_error : ERROR_WRITE_FAULT);
    if (!ok) {
        g_config_persistence_error.store(status.error, std::memory_order_relaxed);
        g_config_persistence_failed.store(true, std::memory_order_release);
    }
    if (pending)
        Log("NR jitter policy migration: changed=%d version=%d persisted=%d "
            "error=%lu. Native SR jitter is unchanged; later manual choices "
            "remain available.", status.jitter_changed,
            nr_config_policy::kJitterVersion, status.persisted, status.error);
}

void RecordConfigPolicySave(bool succeeded, unsigned long error) {
    // Concrete values precede the marker. A successful explicit save can
    // complete a failed migration with the user's current value, including ON.
    g_config_policy_attempted = true;
    g_config_policy_migration.persisted = succeeded;
    g_config_policy_migration.failed = !succeeded;
    g_config_policy_migration.error = succeeded ? ERROR_SUCCESS : error;
}
