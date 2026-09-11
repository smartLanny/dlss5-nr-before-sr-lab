# #85: optional Catmull-Rom edit recovery, fused final composition

This independent slice starts at #172 `987ef37df36e36c631e42825524f12b1411bcbc6`.
Runtime implementation is `c355e70b8042ae8e70b8c02c38e6669248e413f3`; reviewed
generation pins and actual build source are
`1ad47fb8bce2e7ef93a2f38d2213a2b0d2879b39`. No #84 input-kernel or #86 colour
algorithm is included. Target is `next/0.5`; no main/release/push was performed.
At start, remote #172 matched that baseline and next/0.5 was `7540d38…`.

## Production behavior and mathematics

`CatmullRomEditRestore=0` is the default: the existing BilinearSafe low-grid
recovery remains selected. The option has real config read/write/equality and
experimental UI wiring. It only applies with LowGridEditField, Mode2, colour
follow zero, nonzero strength and completed warmup. Input remains Area, model
input/output and log-luminance field representation are unchanged.
All low-grid luma/valid reads use `ReadLumaEditV6(int2)`, currently returning the
existing RG32F field. This is a representation seam for later integration, not
an implementation of colour-field recovery.

For output centre j+1/2 and source extent N / target R, map
`q=(j+1/2)*N/R-1/2`, `i=floor(q)`, `t=q-i`. Four source taps are i-1..i+2,
individually clamped to [0,N-1]. Their Catmull-Rom weights (a=-1/2) are:

```text
w0 = -t/2 + t² - t³/2
w1 = 1 - 5t²/2 + 3t³/2
w2 =  t/2 + 2t² - 3t³/2
w3 = -t²/2 + t³/2
```

Weights sum to one algebraically and are explicitly divided by their floating
sum. Boundary clamping duplicates taps rather than truncating the kernel or
renormalizing around missing data. Signed light/dark edits retain negative
lobes. The horizontal result is stored in **R16G16_FLOAT**, at exactly
**R.width × N.height**, so the log edit is quantized to FP16 once. The second
component is a strict all-four-taps-valid Boolean, including zero-weight taps;
it is never negative-lobe-interpolated as a probability. Each axis also requires
finite data. Vertical recovery reads four horizontal samples with the same
centre/weight/clamp rule, in the final compositor.

If Catmull's footprint is invalid, use the original narrower BilinearSafe
footprint if valid; if that also fails, the existing compositor retains stored
source. There is no valid-weight renormalization, new bilateral kernel or
fabricated confidence. The 1:1 production route skips the intermediate and
retains the exact original BilinearSafe path. A failed allocation/format/PSO or
dimension mismatch similarly leaves a valid existing low-grid Bilinear path;
if the N field itself cannot be prepared, the existing pre-field fallback is
unchanged.

Only raw log-luminance recovery changes. Source-related protection, full-size
edge decisions, lighting/detail stabilization, HDR matched/headroom behavior,
strength budgets, MaxLumaRatio, alpha/subrect and final finite/gamut guards stay
in their original final-resolution code. No full-R field is permanently
materialized in production, and no additional model evaluation is introduced.
The negative lobes can overshoot; no quality or speed improvement is claimed.

## Ownership and recording

The FP16 resource belongs to the existing NrSlot, with its own tracked D3D12
state. Allocation/descriptor writes require `CanMutateSlotLocked`; format load/
typed-UAV-store support and actual driver allocation size are checked. Its
bytes share the existing combined 64 MiB/slot and 256 MiB/Core field quota with
the RG32F N field, including allocation alignment. The slot destructor releases
both and returns both quotas. Existing recorded-list/fence/queue-affinity and
quarantine rules remain responsible for retirement; no new slot pool exists.

The command sequence is N field UAV write → UAV barrier → SRV transition →
horizontal FP16 UAV write → UAV barrier → SRV transition → existing final
composite. Only successful recording sets slot flag 16384 in addition to the
existing 8192 field flag. Both recorded bits reset per before-SR attempt.
Horizontal dispatch has R.width × N.height threads rounded to 8×8 groups;
its constants retain actual R/N extents. Descriptor tables now have seven SRVs
and seven UAVs, including typed-null optional views and shared snapshot writers.

## Actual checks and one coordinated WARP batch

All eight production SM5 entries compiled, as did the test-only reference
entry. Actual clean generation and generated Core C++ frontend compilation
passed. The initial fixture compile caught its obsolete descriptor-count
assertion; it was updated to the new last descriptor and the failure log kept.
No algorithm was changed after source freeze for the WARP run.

The separately authorized batch ran **once**, with D3D12 WARP. It compared
Bilinear, fused Catmull and non-fused reference using identical input/model
planes. The non-fused reference materializes a temporary full-R RG32F field
**only in the fixture**, after the same actual FP16 horizontal output, then
calls the same final compositor. Descriptor reuse between reference stages is
after an actual completion fence. This temporary reference is not production
storage. Declared comparison precision was `3e-5 + 3e-4*max(abs(a),abs(b))` per
component; observed maximum difference was **0**.

80 cases cover R=20×13; N=20×13, 10×6, 13×8, 15×9, 17×11 and 7×5; constant,
ramp, impulse, step, signed checker edits, identity, invalid Neural and linear
HDR fixtures; strengths 0/1/2/4/8 (all non-1 strengths on the 7×5 subset).
Subrect padding and alpha checks use the existing production fixture. Colour,
detail/layer and high-strength-protection settings are identical between paths;
the first attribution batch does not constitute protection-on ablations.

- 480 real raw planes / 2,123,584 bytes retained privately.
- 11,520 FP16 horizontal log/strict-valid samples independently reconstructed
  by Python from actual N field data; all passed the declared FP16 tolerance.
- Deliberately wrong pixel-centre reconstruction differed at 1,966 samples;
  this is an independent CPU negative control, not a GPU barrier fault test.
- 276 invalid-Catmull footprint output pixels exactly matched Bilinear/source
  fallback. Same-grid, zero strength and Neural==Proxy outputs matched Bilinear
  byte-for-byte. Finite values and alpha were checked across the batch.
- 8,275 pixels differed from Bilinear; these are algorithm differences, not a
  quality score. The initial CPU oracle and the later more thorough reanalysis
  both read the same single WARP capture; there was no second GPU run.
- Fixture exited 0, remaining fixture processes 0, GPU access released.

Generated source SHA256:
`4c02025974f54f47d74d579c34bf1629c7655295d6d17c03151bcd9acaf7fa75`.
Source manifest SHA256:
`9f3903b1cab3d7303f178b41c8760de8df794790f99359b2cf62f819f683419c`.
Frozen fixture SHA256:
`6a679ec39f0c721f0b6c9b1036238ae35bba83b3aed5b562022fcd666b365e52`.
All original manifest-listed generated files were rehashed after build: zero
mismatches. Source-manifest/validation, logs and every raw file hash remain in
the external private candidate directory.

## Complete frozen-source build and boundaries

The unchanged generated build script then linked both Core languages and
nrchain successfully. Both Core binaries embed actual source `1ad47fb…` and
retain PE 0.5.0.6/dev6 in this isolated experiment; they do not replace the old
dev6 candidate. nrchain does not embed the Core source identifier.

| Artifact | SHA256 |
|---|---|
| English Core | `08528ed5f39f398352534b3eac23aa4d7e051391e5172800a740c44088dfac7f` |
| Chinese Core | `73815dd67e36b21dc664ec463207f0fb645dff1e8c15d77c7ccfe33af01be3c7` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

Existing compiler/SDK warnings are retained. No full-DLL slot retirement,
Reset/DRS/PSO-allocation fault injection, queue-affinity fault matrix, NVIDIA,
game footage, temporal stability, performance or hosted CI validation was
performed. This batch does not prove general default-shader equivalence against
the frozen old dev6 binary, beyond the stated neutral/1:1 comparisons. It does
not establish better hair/face/neon quality, bounded visible ringing at 4x,
P95/P99 improvement or suitability as the default recovery kernel.

Prepared command for the completed bounded fixture (requires a new evidence
directory and separately coordinated GPU access if ever repeated):

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain -SourceRoot $GeneratedCore `
  -CatmullOutput $NewEvidence
```

Next work is integration review plus the missing lifecycle/protection/visual
evidence, not automatic replay of the old full codec batch or a default change.
