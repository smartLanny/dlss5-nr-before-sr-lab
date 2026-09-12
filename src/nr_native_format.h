#pragma once
#include <array>
#include <cstdint>
#include "interop/nr_format_access.h"
#include "nr_resource_semantics.h"

// Evidence belongs to an object lifetime and one Feature generation. Descriptor
// addresses are deliberately absent: heaps can be copied, overwritten or reused.
namespace nr_native_format {
enum class Role : unsigned { Color, Output };
struct Key {
    std::uint64_t device = 0, resource = 0, feature = 0;
    Role role = Role::Color;
    bool operator==(const Key& b) const noexcept {
        return device == b.device && resource == b.resource &&
               feature == b.feature && role == b.role;
    }
    explicit operator bool() const noexcept { return device && resource && feature; }
};
struct View {
    unsigned format = 0;
    bool conflict = false;
    std::array<unsigned, 6> formats{};
    unsigned count = 0;
    void Observe(unsigned value, bool shape_valid = true) noexcept {
        if (!shape_valid || !value || nr_format_access::IsTypeless(value)) {
            conflict = true;
            return;
        }
        if (format && format != value) conflict = true;
        if (!format) format = value;
        for (unsigned i = 0; i != count; ++i) if (formats[i] == value) return;
        if (count < formats.size()) formats[count++] = value;
        else conflict = true;
    }
};
struct Evidence {
    Key key{};
    View view{};
    std::uint64_t first_call = 0, last_call = 0;
    bool failed = false;
    unsigned Resolved() const noexcept {
        return failed || view.conflict ? 0 : view.format;
    }
};
template <unsigned Capacity = 256> class Ledger {
    std::array<Evidence, Capacity> entries{};
    unsigned next = 0;
public:
    Evidence Find(const Key& key) const noexcept {
        if (key) for (const auto& e : entries) if (e.key == key) return e;
        return {};
    }
    void Commit(const Key& key, const View& view, std::uint64_t call,
                bool success) noexcept {
        if (!key) return;
        Evidence* e = nullptr;
        for (auto& item : entries) if (item.key == key) { e = &item; break; }
        // An invocation which created no view cannot establish an interpretation.
        if (!e && !view.format && !view.conflict && success) return;
        if (!e) {
            e = &entries[next++ % Capacity];
            *e = {};
            e->key = key;
            e->first_call = call;
        }
        if (view.format) e->view.Observe(view.format);
        for (unsigned i = 0; i != view.count; ++i) e->view.Observe(view.formats[i]);
        e->view.conflict |= view.conflict;
        e->failed |= !success;
        e->last_call = call;
    }
};
} // namespace nr_native_format
