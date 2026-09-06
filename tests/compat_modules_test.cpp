#include "../src/nr_compat_modules.h"
#include <cstdio>

int main() {
    using namespace nr_compat_modules;
    unsigned failures = 0;
    const auto check = [&failures](bool ok, const char *label) {
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };

    check(ClassifyAddonFileName(nullptr) == Kind::None, "null path");
    check(ClassifyAddonFileName(L"") == Kind::None, "empty path");
    check(ClassifyAddonFileName(L"obs_capture.addon64") == Kind::None,
          "unrelated addon");
    check(ClassifyAddonFileName(L"renodx-dlssfix.addon64") == Kind::None,
          "dlssfix is a different stem");
    check(ClassifyAddonFileName(L"renodx-dlssnr.addon64") == Kind::None,
          "dlssnr is a different stem");
    check(ClassifyAddonFileName(L"renodx-dlss5.addon64") == Kind::GenericDlss5,
          "stock generic RenoDX DLSS5");
    check(ClassifyAddonFileName(
              L"C:\\mods\\ReNODX-DLSS5_v1.addon64") == Kind::GenericDlss5,
          "versioned generic name is still DLSS5");
    check(ClassifyAddonFileName(L"renodx-dlss5-extra.addon64") ==
              Kind::GenericDlss5,
          "hyphenated generic name");
    check(ClassifyAddonFileName(L"renodx-dlss.addon64") == Kind::ShortFuse,
          "stock ShortFuse / RenoDX DLSS");
    check(ClassifyAddonFileName(
              L"E:\\game\\scripts\\reshade-shaders\\"
              L"renodx-dlss_v20260904.addon64") == Kind::ShortFuse,
          "issue #118 versioned ShortFuse copy");
    check(ClassifyAddonFileName(L"renodx-dlss-nightly.addon64") == Kind::ShortFuse,
          "hyphenated ShortFuse copy");
    check(ClassifyAddonFileName(
              L"DLSS5-AI-beta0.4.2-native-bridge-exp1.addon64") == Kind::None,
          "this project's addon is not a competing owner");

    std::printf("compat modules: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
