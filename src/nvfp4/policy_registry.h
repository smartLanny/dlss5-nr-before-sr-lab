#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace nr::nvfp4::policy {
struct Profile {
    uint32_t vendor_id=0, device_id=0, compute_major=0, compute_minor=0;
    uint32_t sm_count=0, cuda_driver_api_version=0;
};
struct QualifiedPolicy {
    const char* runtime_sha256;
    Profile profile;
    const char* cubin_sha256;
    const char* builder_sha256;
    const char* build_id;
    const char* kernel_id;
    uint32_t variant;
    bool fused_publish;
    std::array<uint32_t,16> tested_m;
    uint32_t tested_m_count;
    // A reviewed qualification artifact identifies the frozen source, original
    // and candidate outputs, acceptance thresholds and real GPU timing.
    const char* qualification_artifact_sha256;
};

// Intentionally empty. Compilation, a launchable kernel, an unsigned local
// JSON file, or a host experiment never qualifies production Auto. Future
// qualification is a reviewed source change to this immutable registry;
// Core integration code and user settings need no new trust mechanism.
inline constexpr std::array<QualifiedPolicy,0> QualifiedPolicies{};
inline constexpr bool HasQualifiedPolicies() { return !QualifiedPolicies.empty(); }
inline const QualifiedPolicy* FindQualifiedPolicy(const char* runtime_sha,const Profile& p) {
    for(const auto& entry:QualifiedPolicies) {
        const auto& wanted=entry.profile;
        if(std::strcmp(runtime_sha,entry.runtime_sha256)==0 && wanted.vendor_id==p.vendor_id && wanted.device_id==p.device_id &&
           wanted.compute_major==p.compute_major && wanted.compute_minor==p.compute_minor && wanted.sm_count==p.sm_count &&
           wanted.cuda_driver_api_version==p.cuda_driver_api_version && entry.tested_m_count && entry.tested_m_count<=entry.tested_m.size() &&
           entry.qualification_artifact_sha256 && std::strlen(entry.qualification_artifact_sha256)==64) return &entry;
    }
    return nullptr;
}
} // namespace nr::nvfp4::policy
