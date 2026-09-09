#include "nr_config_policy.h"

struct ConfigPolicyMigrationStatus {
    bool visible = false;
    bool extra_changed = false;
    bool extra_disabled = false;
    bool jitter_changed = false;
    bool persisted = false;
    bool failed = false;
    unsigned long error = ERROR_SUCCESS;
};

// Protected by g_chain_cs, including the overlay's read-only snapshot.
ConfigPolicyMigrationStatus g_config_policy_migration;
bool g_config_policy_attempted = false;

ConfigPolicyMigrationStatus SnapshotConfigPolicyMigrationStatus() {
    EnterCriticalSection(&g_chain_cs);
    const auto result = g_config_policy_migration;
    LeaveCriticalSection(&g_chain_cs);
    return result;
}

void ApplyConfigPolicyMigrations(ChainConfig &next, bool file_proven_absent) {
    // A failed write may leave old keys on disk. Never replay this migration
    // over this session's manual choices after an explicit successful save.
    if (g_config_policy_attempted) return;
    g_config_policy_attempted = true;
    const bool extra_pending = GetPrivateProfileIntW(
        L"NRBeforeSR", L"ExtraStrengthPolicyVersion", 0, g_ini_path) <
        nr_config_policy::kExtraStrengthVersion;
    const bool jitter_pending = GetPrivateProfileIntW(
        L"NRBeforeSR", L"JitterPolicyVersion", 0, g_ini_path) <
        nr_config_policy::kJitterVersion;
    auto &status = g_config_policy_migration;
    status.visible = !file_proven_absent && (extra_pending || jitter_pending);
    if (extra_pending) {
        const auto plan = nr_config_policy::MigrateExtra(
            next.transfer_strength, next.enabled);
        next.transfer_strength = next.post_transfer_strength = plan.strength;
        next.enabled = plan.enabled;
        status.extra_changed = plan.changed;
        status.extra_disabled = plan.disabled;
    }
    if (jitter_pending) {
        status.jitter_changed = next.jitter_mode != NrJitterMode::Legacy;
        next.jitter_mode = NrJitterMode::Legacy;
    }

    // Shared-strength selection/migration has already run. Its marker must
    // exist before publishing the new policy; failed writes remain visible.
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
    if (extra_pending && ok) {
        // Disable before raising a legacy zero. Any interrupted prefix keeps
        // the old bypass intent, and the completion marker is written last.
        if (status.extra_disabled) write(WriteIniInt(L"Enabled", 0));
        if (ok) write(WriteIniFloat(L"TransferStrength", next.transfer_strength));
        if (ok) write(WriteIniFloat(L"PostTransferStrength", next.post_transfer_strength));
        if (ok) write(WriteIniInt(L"ExtraStrengthPolicyVersion",
                                 nr_config_policy::kExtraStrengthVersion));
    }
    if (jitter_pending && ok) {
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
    if (extra_pending || jitter_pending)
        Log("Independent control policy migration: extraChanged=%d "
            "zeroDisabled=%d jitterChanged=%d persisted=%d error=%lu. "
            "Model controls are unchanged; manual choices remain live.",
            status.extra_changed, status.extra_disabled, status.jitter_changed,
            status.persisted, status.error);
}

void RecordConfigPolicySave(bool succeeded, unsigned long error) {
    // SaveConfig persists the user's current values before both policy keys.
    // A successful explicit save completes a prior failed migration without
    // forcing jitter off or reapplying the strength migration to live values.
    g_config_policy_attempted = true;
    g_config_policy_migration.persisted = succeeded;
    g_config_policy_migration.failed = !succeeded;
    g_config_policy_migration.error = succeeded ? ERROR_SUCCESS : error;
}
