# Corefix.7 retest: current format evidence is absent

The owner reports no visible response in the latest ZZZ session. The loaded
Chinese Core matches the delivered Corefix.7 artifact; this is a new session,
not the earlier launcher/helper failure. Frozen runtime source is
`7d68fef12958764b05429eec98f18e9cb134a73b`, with packaging documentation at
`f07a867fb00502e47a4f2ef8b5656dc323194654`.

## Bounded current evidence

The F8 window remains at config 8 / Feature generation 5. Carrier Evaluate
increases from 1278 to 1309, while NR remains 160 and transfer submissions
remain 157. The successful NR parameter receipt belongs to earlier config 3 /
Feature generation 2. It is not evidence of current NR activity.

The current typeless Color and Output resources have no accepted typed view.
The native Output Clear is observed and native SR returns successfully, but
both its CPU and GPU descriptor receipts report `slot-unobserved-or-evicted`.
The CPU pool contains 15,771 records out of 32,768, with no eviction, drop,
ambiguity, or coverage-loss flag. The GPU pool is full and has many evictions.
The registry reports 15 global invalidations. These are cumulative counts;
the report does not name each invalidating call or its destination ranges.

## Source discriminator

`Registry::Read` uses the same unknown-slot reason for a missing slot and a
slot whose epoch predates a global invalidation. `InvalidateLocked` advances
that epoch without removing records or setting a pool coverage-loss flag.
Consequently, a non-full CPU pool with no evictions can still lose all usable
view evidence.

The native Copy observer globally invalidates when a copy exceeds 256
descriptors or 64 source/destination ranges. Those observation limits are not
D3D12 copy limits. A copy writes its destination descriptors; unrelated CPU
source descriptors do not change. Microsoft's [descriptor copy contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors)
also permits sparse ranges and requires non-shader-visible sources.

The frozen Corefix.7 production translation unit reproduces that defect with
the real `NrDescriptorBeginCopy` function and a 257-descriptor request. An
unrelated stable CPU UAV changes from known to `SlotUnknown`, while CPU pool
occupancy remains 3/32,768, CPU evictions remain zero, and global invalidations
increase from zero to one. The complete synthetic-COM run passes 164 assertions;
it creates no D3D device, executes no GPU work, and calls no NGX model.

Two earlier fixture placements are retained as unsuccessful setup attempts:
one invalidated evidence still needed by following tests, and one ran after
the test heap had already retired. Moving the counterexample before retirement
and after the dependent checks changes only test placement, not production
code. The final source-level counterexample proves the observer defect; without
a trace of the 15 game calls, it cannot identify every invalidation in this
session or prove final game visuals.

Raw feedback, the owner photo, local paths, and artifact verification receipts
remain private. No game files were changed during this diagnosis.
