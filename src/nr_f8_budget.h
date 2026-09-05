#pragma once
#include <atomic>
#include <cstdint>
namespace nr_f8_budget {
constexpr std::uint64_t kGpuLimit = 128ull * 1024 * 1024;
constexpr std::uint64_t kCpuLimit = 128ull * 1024 * 1024;
constexpr std::uint64_t kPayloadLimit = 512ull * 1024 * 1024;
constexpr std::uint64_t kWorkerReserve = 64ull * 1024;
inline std::atomic<std::uint64_t> gpu_bytes{0}, cpu_bytes{0};
inline bool Reserve(std::atomic<std::uint64_t> &counter, std::uint64_t bytes,
                    std::uint64_t limit) {
    auto current = counter.load();
    do { if (bytes > limit || current > limit - bytes) return false; }
    while (!counter.compare_exchange_weak(current, current + bytes));
    return true;
}
inline bool ReservePair(std::uint64_t bytes) {
    if (!Reserve(gpu_bytes, bytes, kGpuLimit)) return false;
    if (Reserve(cpu_bytes, bytes+kWorkerReserve, kCpuLimit-kWorkerReserve)) return true;
    gpu_bytes.fetch_sub(bytes); return false;
}
inline void ReleasePair(std::uint64_t bytes) {
    gpu_bytes.fetch_sub(bytes); cpu_bytes.fetch_sub(bytes+kWorkerReserve);
}
}
