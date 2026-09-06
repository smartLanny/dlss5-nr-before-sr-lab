#include "../src/nr_dx11_observer_status.h"
#include <cstdio>

int main() {
    using namespace nr_dx11_observer_status;
    unsigned failures = 0;
    const auto check = [&failures](bool ok, const char *label) {
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };

    Sample d3d12{};
    d3d12.presenting = PresentingApi::D3d12;
    d3d12.d3d12_hooks_ready = true;
    check(Classify(d3d12) == Class::NotDx11,
          "D3D12 present stays on the Feature 1 wait path");

    Sample installing{};
    installing.presenting = PresentingApi::D3d11;
    check(Classify(installing) == Class::WaitingForHooks,
          "D3D11 present before hooks are ready is not terminal");

    Sample issue118{};
    issue118.presenting = PresentingApi::D3d11;
    issue118.d3d12_hooks_ready = true;
    check(Classify(issue118) == Class::Dx11PresentNoD3d12Carrier,
          "healthy D3D12 hooks + D3D11 present + zero Feature 1 is not waiting-feature1");

    Sample unbridged = issue118;
    unbridged.d3d11_feature1_success = 1;
    check(Classify(unbridged) == Class::Dx11Feature1Unbridged,
          "observed D3D11 Feature 1 still cannot feed the D3D12 NR core");

    Sample bridged = issue118;
    bridged.d3d12_carrier_evals = 4;
    check(Classify(bridged) == Class::D3d12CarrierPresent,
          "a live D3D12 carrier wins even on a D3D11 present");

    Sample created = issue118;
    created.d3d12_feature1_success = 1;
    check(Classify(created) == Class::D3d12CarrierPresent,
          "a successful D3D12 Feature 1 create is a real carrier");

    check(PresentingApiName(PresentingApi::D3d11)[0] == 'd',
          "presenting API name is d3d11");
    check(ClassName(Class::Dx11PresentNoD3d12Carrier)[0] != '\0',
          "class name is non-empty");

    std::printf("dx11 observer status: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
