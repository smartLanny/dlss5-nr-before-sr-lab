#pragma once
// Host-only preparation primitive; NOT a CUDA/NGX ABI or a live hook.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace nr::nvfp4 {
// Resolve manifest strings into these indices once, never during recording.
enum class BufferId : std::uint8_t { weights, input, input_scale, hidden, hidden_scale, output, workspace, count };
struct BufferRange { std::uint64_t address = 0, bytes = 0; };
using Bindings = std::array<BufferRange, static_cast<std::size_t>(BufferId::count)>;
struct Patch {
    std::size_t offset = 0;
    BufferId buffer = BufferId::weights;
    std::uint64_t addend = 0;
    std::uint64_t required_bytes = 0;
    std::uint64_t alignment = 1;
};
enum class Error { none, invalid_template, invalid_patch, overlap, buffer_missing, range, address_overflow, alignment, output_size };

class ParameterPlan {
public:
    // Allocation and validation belong to resource/shape preparation, before
    // *any* replacement work is recorded. Plan is immutable after construction.
    static Error Build(const std::vector<std::uint8_t>& bytes,
                       const std::vector<Patch>& patches, ParameterPlan& out) {
        if (bytes.empty() || bytes.size() > 65536 || patches.empty())
            return Error::invalid_template;
        std::vector<bool> used(bytes.size(), false);
        for (const Patch& p : patches) {
            if (static_cast<std::size_t>(p.buffer) >= static_cast<std::size_t>(BufferId::count) ||
                p.offset > bytes.size() || bytes.size() - p.offset < sizeof(std::uint64_t) ||
                p.required_bytes == 0 || p.alignment == 0 || (p.alignment & (p.alignment - 1)))
                return Error::invalid_patch;
            for (std::size_t i = 0; i < 8; ++i) {
                if (used[p.offset + i]) return Error::overlap;
                used[p.offset + i] = true;
            }
        }
        ParameterPlan next;
        next.template_ = bytes;
        next.patches_ = patches;
        out = std::move(next);
        return Error::none;
    }
    std::size_t size() const noexcept { return template_.size(); }
    // Does not allocate, stringify, consult a map or mutate out on failure.
    // The caller must own out until the API's CPU-parameter consumption ends;
    // GPU buffers/descriptors remain fence-owned separately.
    Error Bind(const Bindings& buffers, std::uint8_t* out, std::size_t size) const noexcept {
        if (template_.empty() || !out || size != template_.size()) return Error::output_size;
        for (const Patch& p : patches_) {
            const BufferRange b = buffers[static_cast<std::size_t>(p.buffer)];
            if (!b.address || !b.bytes) return Error::buffer_missing;
            if (p.addend > b.bytes || p.required_bytes > b.bytes - p.addend) return Error::range;
            constexpr auto max = std::numeric_limits<std::uint64_t>::max();
            if (p.addend > max - b.address || p.required_bytes - 1 > max - (b.address + p.addend))
                return Error::address_overflow;
            if ((b.address + p.addend) % p.alignment) return Error::alignment;
        }
        std::memcpy(out, template_.data(), template_.size());
        for (const Patch& p : patches_) {
            const auto address = buffers[static_cast<std::size_t>(p.buffer)].address + p.addend;
            // Explicit little-endian descriptor bytes: no unaligned uint64_t store.
            for (std::size_t j = 0; j < 8; ++j)
                out[p.offset + j] = static_cast<std::uint8_t>(address >> (j * 8));
        }
        return Error::none;
    }
private:
    std::vector<std::uint8_t> template_;
    std::vector<Patch> patches_;
};

// Only a fail-closed transaction model. It does NOT make previously recorded
// GPU commands reversible and is NOT connected to the production add-on.
class SegmentTransaction {
public:
    enum class State { collecting, ready, recording, submitted, retired, bypassed, quarantined };
    State state() const noexcept { return state_; }
    bool original_is_legal() const noexcept { return state_ == State::collecting || state_ == State::ready || state_ == State::bypassed; }
    bool Prepare(bool semantic_proof, bool resources_ready, bool measured_win) noexcept {
        if (state_ != State::collecting) return false;
        state_ = semantic_proof && resources_ready && measured_win ? State::ready : State::bypassed;
        return state_ == State::ready;
    }
    bool BeginRecording() noexcept {
        if (state_ != State::ready) return false;
        state_ = State::recording; // Before first command, not after launch success.
        return true;
    }
    void Fail() noexcept {
        if (state_ == State::retired || state_ == State::quarantined) return;
        state_ = original_is_legal() ? State::bypassed : State::quarantined;
    }
    bool Submitted(std::uint64_t queue, std::uint64_t value) noexcept {
        if (state_ != State::recording || !queue || !value) { Fail(); return false; }
        queue_ = queue; fence_ = value; state_ = State::submitted; return true;
    }
    bool Retire(std::uint64_t queue, std::uint64_t completed, bool replay_references_drained) noexcept {
        if (state_ != State::submitted || !replay_references_drained || queue != queue_ || completed < fence_)
            return false;
        state_ = State::retired; return true;
    }
private:
    State state_ = State::collecting;
    std::uint64_t queue_ = 0, fence_ = 0;
};
} // namespace nr::nvfp4
