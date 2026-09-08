#include <cstdlib>
#include <iostream>

#include "../../src/interop/nr_external_provider_producer.h"

namespace {

struct QueryScript {
    enum class Mode {
        DelayedReady,
        LegacyReject,
        AlwaysStarting,
        FixedReason,
        OddReadyEpoch,
        InvalidSize,
        InvalidAbi,
        InvalidReserved,
        CallFailure,
    } mode = Mode::DelayedReady;
    NrExternalProviderReason reason = NrExternalProviderReason::Starting;
    unsigned calls = 0;
    bool bad_arguments = false;
};

QueryScript g_script;

std::int32_t WINAPI QueryStub(std::uint32_t abi, std::uint32_t size,
                              std::uint32_t flags,
                              NrExternalStatusV1 *output) {
    ++g_script.calls;
    if (abi != kNrExternalProviderAbi || size != sizeof(NrExternalStatusV1) ||
        flags != kNrExternalQueryNativeObserver || !output) {
        g_script.bad_arguments = true;
        return 0;
    }
    if (g_script.mode == QueryScript::Mode::LegacyReject ||
        g_script.mode == QueryScript::Mode::CallFailure)
        return 0;

    *output = NrExternalStatusV1{};
    switch (g_script.mode) {
    case QueryScript::Mode::DelayedReady:
        output->reason = g_script.calls < 3
            ? (g_script.calls == 1 ? NrExternalProviderReason::Starting
                                   : NrExternalProviderReason::Busy)
            : NrExternalProviderReason::Ready;
        output->observer_epoch = 8;
        break;
    case QueryScript::Mode::AlwaysStarting:
        output->reason = NrExternalProviderReason::Starting;
        break;
    case QueryScript::Mode::FixedReason:
        output->reason = g_script.reason;
        break;
    case QueryScript::Mode::OddReadyEpoch:
        output->reason = NrExternalProviderReason::Ready;
        output->observer_epoch = 9;
        break;
    case QueryScript::Mode::InvalidSize:
        output->reason = NrExternalProviderReason::Ready;
        output->size = sizeof(NrExternalStatusV1) - 1;
        break;
    case QueryScript::Mode::InvalidAbi:
        output->reason = NrExternalProviderReason::Ready;
        output->abi ^= 1;
        break;
    case QueryScript::Mode::InvalidReserved:
        output->reason = NrExternalProviderReason::Ready;
        output->reserved = 1;
        break;
    case QueryScript::Mode::LegacyReject:
    case QueryScript::Mode::CallFailure:
        break;
    }
    return 1;
}

[[noreturn]] void Fail(const char *message) {
    std::cerr << "FAIL " << message << '\n';
    std::exit(1);
}

void Check(bool condition, const char *message) {
    if (!condition) Fail(message);
}

void Reset(QueryScript::Mode mode) {
    g_script = {};
    g_script.mode = mode;
}

void ExpectImmediateDecline(QueryScript::Mode mode,
                            NrExternalProviderReason reason,
                            const char *message) {
    Reset(mode);
    g_script.reason = reason;
    NrExternalStatusV1 status{};
    Check(!nr_external_provider_client::WaitForNativeObserver(
              QueryStub, 50, status),
          message);
    Check(g_script.calls == 1 && !g_script.bad_arguments,
          "terminal state must decline after one exact native-preflight query");
    Check(status.reason == reason, "terminal reason must remain observable");
}

} // namespace

int main() {
    NrExternalStatusV1 status{};

    Reset(QueryScript::Mode::DelayedReady);
    Check(nr_external_provider_client::WaitForNativeObserver(QueryStub, 100, status),
          "delayed observer readiness must succeed within the deadline");
    Check(g_script.calls == 3 && !g_script.bad_arguments,
          "startup wait must poll with ABI bit 2 through Starting and Busy");
    Check(status.reason == NrExternalProviderReason::Ready &&
              status.observer_epoch == 8,
          "successful wait must return the ready even epoch");

    Reset(QueryScript::Mode::LegacyReject);
    Check(!nr_external_provider_client::WaitForNativeObserver(QueryStub, 50, status),
          "an old flags-0/1 provider must reject native preflight closed");
    Check(g_script.calls == 1 && !g_script.bad_arguments,
          "legacy rejection must not retry or downgrade to the old query mode");

    Reset(QueryScript::Mode::AlwaysStarting);
    Check(!nr_external_provider_client::WaitForNativeObserver(QueryStub, 0, status),
          "a zero deadline must reject a still-starting observer");
    Check(g_script.calls == 1 && status.reason == NrExternalProviderReason::Starting,
          "timeout must retain the last starting status without authorizing create");

    ExpectImmediateDecline(QueryScript::Mode::FixedReason,
                           NrExternalProviderReason::ObserverFailed,
                           "startup failure must decline");
    ExpectImmediateDecline(QueryScript::Mode::FixedReason,
                           NrExternalProviderReason::Stopping,
                           "observer shutdown must decline");
    ExpectImmediateDecline(QueryScript::Mode::FixedReason,
                           NrExternalProviderReason::ObserverUncertain,
                           "observer uncertainty must decline");

    Reset(QueryScript::Mode::OddReadyEpoch);
    Check(!nr_external_provider_client::WaitForNativeObserver(QueryStub, 50, status),
          "Ready with an odd observer epoch must decline");
    Check(g_script.calls == 1 && status.observer_epoch == 9,
          "odd ready epoch must not be hidden or retried");

    for (const auto mode : {QueryScript::Mode::InvalidSize,
                            QueryScript::Mode::InvalidAbi,
                            QueryScript::Mode::InvalidReserved,
                            QueryScript::Mode::CallFailure}) {
        Reset(mode);
        Check(!nr_external_provider_client::WaitForNativeObserver(QueryStub, 50, status),
              "invalid query reply must fail closed");
        Check(g_script.calls == 1 && !g_script.bad_arguments,
              "invalid reply must be rejected after one exact query");
    }

    Check(!nr_external_provider_client::WaitForNativeObserver(nullptr, 50, status),
          "a missing query export must fail closed");

    std::cout << "PASS external provider startup client: delayed-ready, legacy ABI, "
                 "timeout, terminal states, epochs and reply validation\n";
    return 0;
}
