#include <atomic>
#include <cassert>
#include <cstdio>
#include "src/interop/nr_external_provider_abi.h"

std::atomic<bool> g_observer_stopping{false}, g_observer_startup_failed{false};
std::atomic<bool> g_evaluate_hooks_ready{false}, g_module_rescan_in_progress{false};
std::atomic<bool> g_module_observer_gap_detected{false}, g_feature_tracking_uncertain{false};
std::atomic<unsigned long long> g_module_observer_epoch{0};
int polls = 0;
bool fail_late_coverage = false;
void PollNgxLifecycleModules(bool force) {
    assert(force); ++polls;
    g_module_observer_epoch += 2;
    if (fail_late_coverage) g_module_observer_gap_detected = true;
}
// Actual production preflight; only module installation is simulated here.
#include "src/interop/nr_external_observer_startup.inl"
int main() {
    using R = NrExternalProviderReason;
    for (int i = 0; i != 100; ++i)
        assert(QueryNativeObserverStartup().reason == R::Starting);
    assert(polls == 0); // Early Create is never authorized by an epoch-zero reply.
    g_observer_startup_failed = true;
    assert(QueryNativeObserverStartup().reason == R::ObserverFailed);
    g_observer_startup_failed = false;
    g_evaluate_hooks_ready = true;
    auto ready = QueryNativeObserverStartup();
    assert(ready.reason == R::Ready && ready.observer_epoch == 2 && polls == 1);
    assert(ready.owner_provider_id == 0); // Readiness does not claim an NR owner.
    fail_late_coverage = true;
    assert(QueryNativeObserverStartup().reason == R::ObserverUncertain);
    fail_late_coverage = false; g_module_observer_gap_detected = false;
    g_feature_tracking_uncertain = true;
    assert(QueryNativeObserverStartup().reason == R::ObserverUncertain);
    g_feature_tracking_uncertain = false; g_module_rescan_in_progress = true;
    assert(QueryNativeObserverStartup().reason == R::ObserverUncertain);
    g_module_rescan_in_progress = false; g_module_observer_epoch = 1;
    assert(QueryNativeObserverStartup().reason == R::ObserverUncertain);
    g_observer_stopping = true;
    const auto before = polls;
    assert(QueryNativeObserverStartup().reason == R::Stopping && polls == before);
    puts("production observer preflight: early publication, failed install, late coverage, unknown handle, odd epoch, stopping passed");
}
