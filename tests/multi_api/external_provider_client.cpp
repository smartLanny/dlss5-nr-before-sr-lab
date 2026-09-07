#include "src/interop/nr_external_provider_producer.h"
#include <cstdio>
#include <cstdlib>

// A Windows export/discovery peer, not an NR implementation. Exercises the
// actual producer client without a GPU or fabricated Feature18 success.
static NrExternalProviderReason peer_reason = NrExternalProviderReason::Ready;
static std::uint64_t peer_owner = 0;
static bool peer_available = true;
static unsigned claims = 0, submits = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_QueryV1(
    std::uint32_t, std::uint32_t, std::uint32_t, NrExternalStatusV1 *out) {
    if (!peer_available) return 0;
    *out = NrExternalStatusV1{};
    out->reason = peer_reason;
    out->owner_provider_id = peer_owner;
    out->observer_epoch = 2;
    return 1;
}
extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_ClaimOwnershipV1(
    std::uint32_t abi, std::uint32_t size, std::uint64_t id, NrExternalStatusV1 *out) {
    ++claims;
    peer_owner = id;
    return NRExternalProvider_QueryV1(abi, size, 0, out);
}
extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_ReleaseOwnershipV1(
    std::uint32_t, std::uint64_t) { peer_owner = 0; return 1; }
extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_SubmitFrameV1(
    std::uint32_t, const NrExternalFrameV1 *, NrExternalSubmitResultV1 *) {
    ++submits;
    return 0; // Never call a copy "NR" in this test.
}

int main() {
    using namespace nr_external_provider_client;
    CHECK(!ShouldDisableFeederNr());
    CHECK(Claim(kNrExternalProviderIdDl5f));
    CHECK(retained != nullptr); // Discovered actual exports in this executable.
    CHECK(ShouldDisableFeederNr());
    CHECK(CanPrepareFrame());
    CHECK(Claim(kNrExternalProviderIdDl5f));
    CHECK(claims == 1); // Repeated per-frame preflight cannot reset consumer history.
    CHECK(!Claim(123));
    peer_reason = NrExternalProviderReason::Disabled;
    CHECK(!CanPrepareFrame());
    CHECK(submits == 0); // F6 preflight never calls Submit.
    CHECK(ShouldDisableFeederNr());
    peer_reason = NrExternalProviderReason::ObserverUncertain;
    CHECK(!CanPrepareFrame());
    CHECK(ShouldDisableFeederNr());
    peer_available = false;
    CHECK(!CanPrepareFrame());
    CHECK(ShouldDisableFeederNr());
    peer_available = true;
    peer_reason = NrExternalProviderReason::Ready;
    CHECK(CanPrepareFrame());
    AcquireSRWLockExclusive(&lock);
    CHECK(!CanPrepareFrame());
    CHECK(ShouldDisableFeederNr());
    ReleaseSRWLockExclusive(&lock);
    Release();
    CHECK(!CanPrepareFrame());
    CHECK(ShouldDisableFeederNr()); // Teardown still cannot revive private NGX.
    CHECK(Claim(kNrExternalProviderIdDl5f));
    CHECK(claims == 2);
    CHECK(CanPrepareFrame());
    Release();
    std::printf("%u Windows producer export/preflight/ownership assertions passed; no NR evaluation\n", checks);
}
