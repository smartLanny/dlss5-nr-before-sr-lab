// Extends the real-mutation/reader fixture, not a replacement state algorithm.
// The runner counts registry walks in a temporary extracted source only.
static unsigned long long registry_walks = 0;
#define main lockperf_fixture_main
#include "command_state_lockperf.cpp"
#undef main

static void other_setter(ID3D12GraphicsCommandList *list) {
    MutateNativeCommandState(list, [](NativeCommandState &s) {
        ++s.value; s.check = s.value ^ 0x55AAu;
    });
}
static int cache_budget() {
    initialize();
    // Put watched lists near the END of the real 2048-slot registry. This
    // reproduces switching lists without assuming a favourable slot layout.
    {
        Exclusive lock;
        for (unsigned i = 0; i < 4; ++i) {
            g_command_states[2044 + i] = g_command_states[i];
            g_command_states[i] = {};
        }
        ++g_command_state_epoch;
    }
    registry_walks = 0;
    for (unsigned round = 0; round < 2000; ++round) {
        for (unsigned i = 0; i < 4; ++i) {
            bump(&lists[i]);
            other_setter(&lists[i]); // Different Mutation template instantiation.
        }
    }
    for (unsigned i = 0; i < 4; ++i) assert(states[i].value == 4000);
    std::cout << "four-list/two-setter registry walks=" << registry_walks << " (budget 4)\n";
    if (registry_walks > 4) return 3;

    ID3D12GraphicsCommandList unknown[4]{};
    registry_walks = 0;
    for (unsigned r = 0; r < 2000; ++r)
        for (auto &list : unknown) { bump(&list); other_setter(&list); }
    std::cout << "four-unwatched-list walks=" << registry_walks << " (budget 4)\n";
    assert(registry_walks == 4);
    // Republish one cached negative, then reuse its slot AND native address.
    {
        Exclusive lock;
        g_command_states[2043] = {8000, &unknown[2], &states[20], true};
        ++g_command_state_epoch;
    }
    bump(&unknown[2]); assert(states[20].value == 1);
    {
        Exclusive lock;
        g_command_states[2043] = {};
        g_command_states[2042] = {8001, &unknown[2], &states[21], true};
        ++g_command_state_epoch;
    }
    other_setter(&unknown[2]);
    assert(states[20].value == 1 && states[21].value == 1);
    // Capacity overflow must only cost a lookup, never lose a mutation.
    initialize();
    for (unsigned r = 0; r < 100; ++r)
        for (unsigned i = 0; i < 8; ++i) bump(&lists[i]);
    for (unsigned i = 0; i < 8; ++i) assert(states[i].value == 100);
    // hooks_ready changes need no epoch: cached records are rechecked under lock.
    { Exclusive lock; g_command_states[7].hooks_ready = false; }
    bump(&lists[7]); assert(states[7].value == 100);
    { Exclusive lock; g_command_states[7].hooks_ready = true; }
    bump(&lists[7]); assert(states[7].value == 101);
    std::cout << "cache budget, negatives, epoch, address/slot reuse, overflow and hooks-ready: passed\n";
    return 0;
}
int main() { return cache_budget(); }
