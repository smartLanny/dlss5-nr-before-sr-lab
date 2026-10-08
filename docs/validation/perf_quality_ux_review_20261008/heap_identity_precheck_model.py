#!/usr/bin/env python3
"""CPU model of a proposed redundant descriptor-heap identity precheck.

This is NOT extracted/compiled production C++, a D3D12 test, or a benchmark.
The reference models CommandStateSetHeaps at main 024f9cc3 (lines 240-270).
It tests state equivalence and metadata-call counts, not COM ownership,
concurrency, invalid native memory, GPU behaviour, or actual performance.

Run: python3 heap_identity_precheck_model.py
No files, game settings, runtime DLLs, or GPU state are modified.
"""
from __future__ import annotations

import itertools
import json
from dataclasses import dataclass, replace
from typing import Callable


@dataclass(frozen=True, eq=False)
class Heap:
    name: str
    kind: int


@dataclass(frozen=True)
class State:
    heaps: tuple[Heap | None, Heap | None]
    heap_domain_unknown: bool
    known: bool
    reason: str


Values = tuple[Heap | None, ...] | None
Predicate = Callable[[State, int, Values], bool]


def reference(state: State, count: int, values: Values) -> tuple[State, int]:
    """Model the inspected function's state decisions and GetDesc calls."""
    calls = 0

    def invalid() -> tuple[State, int]:
        return replace(state, known=False, reason="invalid-recorded-state"), calls

    if count > 2 or (count != 0 and values is None):
        return invalid()
    next_heaps: list[Heap | None] = [None, None]
    for i in range(count):
        assert values is not None and len(values) >= count
        heap = values[i]
        if heap is None:
            return invalid()
        calls += 1
        if heap.kind > 1 or next_heaps[heap.kind] is not None:
            return invalid()
        next_heaps[heap.kind] = heap
    pair = (next_heaps[0], next_heaps[1])
    if state.heaps == pair and not state.heap_domain_unknown:
        return state, calls
    return replace(state, heaps=pair, heap_domain_unknown=False), calls


def precheck(state: State, count: int, values: Values) -> bool:
    """Proposed early no-op; changed/invalid/unknown cases use reference."""
    if state.heap_domain_unknown:
        return False
    first, second = state.heaps
    if count == 0:
        return first is None and second is None
    if values is None:
        return False
    if count == 1:
        heap = values[0]
        return heap is not None and (
            (heap is first and second is None)
            or (heap is second and first is None)
        )
    if count == 2:
        a, b = values[:2]
        return a is not None and b is not None and a is not b and (
            (a is first and b is second) or (a is second and b is first)
        )
    return False


def candidate(state: State, count: int, values: Values,
              predicate: Predicate = precheck) -> tuple[State, int]:
    return (state, 0) if predicate(state, count, values) else reference(state, count, values)


def main() -> None:
    a, b = Heap("cbv-a", 0), Heap("cbv-b", 0)
    s, t = Heap("sampler-a", 1), Heap("sampler-b", 1)
    other = Heap("invalid-type", 2)
    values_pool = (None, a, b, s, t, other)
    inputs: list[tuple[int, Values]] = [(0, None), (0, ()), (0, (None,)), (0, (a, s))]
    for count in (1, 2, 3):
        inputs.append((count, None))
        inputs.extend((count, v) for v in itertools.product(values_pool, repeat=count))
    inputs.append((2**32 - 1, None))
    tested = 0
    shortcuts = 0
    baseline_queries = 0
    candidate_queries = 0
    for first, second, unknown, known in itertools.product(
            (None, a, b), (None, s, t), (False, True), (False, True)):
        state = State((first, second), unknown, known, "none" if known else "earlier-unknown")
        for count, values in inputs:
            expected, before = reference(state, count, values)
            actual, after = candidate(state, count, values)
            assert actual == expected, (state, count, values, expected, actual)
            assert after <= before
            tested += 1
            shortcuts += int(precheck(state, count, values))
            baseline_queries += before
            candidate_queries += after

    # Negative controls: each incorrect predicate must differ from the reference.
    unknown_state = State((a, s), True, True, "none")
    def omit_unknown_guard(state: State, count: int, values: Values) -> bool:
        return precheck(replace(state, heap_domain_unknown=False), count, values)
    assert candidate(unknown_state, 2, (a, s), omit_unknown_guard)[0] != reference(unknown_state, 2, (a, s))[0]

    single_state = State((a, None), False, True, "none")
    def incorrect_set_comparison(state: State, count: int, values: Values) -> bool:
        if state.heap_domain_unknown or values is None or count > 2:
            return False
        return {id(h) for h in values[:count] if h is not None} == {
            id(h) for h in state.heaps if h is not None}
    assert candidate(single_state, 2, (a, a), incorrect_set_comparison)[0] != reference(single_state, 2, (a, a))[0]

    assert reference(single_state, 1, (a,))[1] == 1
    assert candidate(single_state, 1, (a,))[1] == 0
    dual_state = State((a, s), False, True, "none")
    assert reference(dual_state, 2, (s, a))[1] == 2
    assert candidate(dual_state, 2, (s, a))[1] == 0
    print(json.dumps({
        "scope": "independent CPU decision model; not production C++ or D3D12",
        "source_ref": "024f9cc32778c1056db364d76d087ee257da6502",
        "source_file": "src/nr_command_state.inl",
        "source_symbol": "CommandStateSetHeaps",
        "equivalence_cases_passed": tested,
        "negative_controls_detected": 2,
        "synthetic_precheck_hits": shortcuts,
        "synthetic_baseline_GetDesc_calls": baseline_queries,
        "synthetic_candidate_GetDesc_calls": candidate_queries,
        "repeated_single_heap_GetDesc_calls": [1, 0],
        "repeated_two_heaps_GetDesc_calls": [2, 0],
        "fps_gpu_timing_and_cpp_compilation": "not tested",
        "assumptions": [
            "Stored heap pointers remain strongly owned under the existing state locks.",
            "A live heap's descriptor type is immutable; pointer identity is not a global address cache.",
            "The model excludes invalid native memory and does not prove COM lifetime or concurrency."
        ]
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
