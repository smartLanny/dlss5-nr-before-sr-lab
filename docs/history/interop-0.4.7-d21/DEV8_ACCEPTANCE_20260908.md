# 0.5 dev8 configuration/wiring acceptance slice

This continues #172 at `229c908c47bd2cff0e6a464516fb86da725a5c5c` without
restarting its input/restoration/EditField algorithms. The fixed execution
instruction is plan `0838ffb549a069dbcef9ac02914907be851d805d`. No main merge,
Manager payload replacement, new carrier, multi-NR, or game acceptance follows.

## Actual production and build

The configuration implementation is `d4ec6f09b696300fdaa03e5c4b79f10340cef61b`;
`888352bda8a776657bfaad2b530efaa198c3e3e5d` assigns a separate dev8 identity and
adds the focused build gate. The final successful exact-source build is
**`61beae66fb42bcab62649576236b651c2f3cc421`**, beta0.5-dev8 / PE 0.5.0.8.
Later packaging, test and documentation commits are not the DLL source.

The existing work-config admission now preserves prior valid requests on invalid
or malformed input. New absent-file installs start at 100%; existing valid INI
settings retain their prior meaning. The explicit recommended-settings action
still selects Quality/75%, distinct from a new install. Recommendation clears
all three experimental kernel/field requests and hidden layer gains. Existing
history/signature/config-generation handling remains the only owner. This does
not merge the separate full R/N/O receipt work from #183.

Actual source edits are listed in PR172_B_LOCAL_HANDOFF_20260908.md. The old
handoff's incomplete build is historical and is superseded by the batch here.
Experimental Lanczos input, Catmull restoration and colour EditField remain
default off. Single NR and effect 1.0 remain the recommended inference count and
strength; no claimed approximation to two-NR visual quality is made.

## Focused validation on ROG Windows

`validate-050-acceptance.ps1` performed canonical, pinned full source generation,
then real Win32 work configuration (67 assertions), recommended settings (468),
its independent negative control, full post transformation (8 tests, no skip),
picture integration (5), EditField (95 assertions), colour field (19,950), all
11 production SM5 entries, complete bilingual generated Core/nrchain linking,
and the 13-group combined low-grid WARP fixture. All passed on the final source.

The eight all-off byte comparisons used the raw shader from the frozen #172
229c908 source. The fixture's inherited print label says dev6-off, but this batch
does not claim a dev6 binary as its reference. Remaining groups cover RG luma,
colour-zero RG + Catmull, RGBA opponent colour, finite Lanczos Proxy/prefix and
unknown-domain invalid input. WARP uses constructed neural planes, not NVIDIA
Feature18. No repeated historical 384-case matrix or old independent 200/80/30
GPU runs are claimed for this batch.

The first dev8 attempt stopped at an old literal build-script pin in the picture
fixture. The fixture was updated to the actual dev8 blob, retaining its source
lock; full source was explicitly provided to the post test. The failed attempt
and original logs remain locally preserved. Nothing was reclassified as pass.
The existing 70,918-byte shader-literal warning remains; not warning-free.

| Evidence | SHA256 |
| --- | --- |
| Source manifest | `91c14c9c8b79804cfa871bcda5039e31051b2edefec5fb8598d80e8a5689be65` |
| Frozen reference source | `a435f318559a7e38c248a7a0d6498aa2f98efe5f75a56006bd5126f16e0b402f` |
| Compiler executable | `bf58cf0e544a308660645aad1fe65ae9cbc44b0871e4010278f3d9551f86a690` |
| English Core (1,427,968 bytes) | `e2d5e39c17e230f10f8307854edd8900b02a8ac5849efce3eea3be65219cb247` |
| Chinese Core (1,453,056 bytes) | `447cef373215f9d376abcfc094ee8c3c19babd72dad76ec4177a4860a3c6fc7f` |
| nrchain (8,192 bytes) | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| Full successful transcript | `aafed1e10e0956557525262f3fdf9a2ed0b6d1c84fd4a5ca133553aa9364536a` |

ReShade is locked to `4a50d1eddace85734871d91792ff214f13f66c01`, LLVM-MinGW
20260826. No source/dependency mismatch was found when verifying the generated
manifest after compilation. The exact local command/evidence stays in the
worktree's separate output directory; raw logs are not source payload.

## Delivery boundary

The separate `package-050-core-acceptance.py` requires the focused evidence and
all hashes, rejects changed build inputs, preserves existing output, and verifies
ZIP inner bytes. It does NOT relax the full multi-API packager's carrier and
post-scaled-work gates. Each language's six-file manual acceptance package
contains only Core/nrchain, notices, user guide and hash/provenance records.
No carrier, replacement INI, SDK, model, raw logs or private local path is shipped.

This scope is **D3D12 Core-only manual acceptance, not Manager OTA**. A's new
DX11 carrier is still independent. Default Manager Core is unchanged.
See [maintainer guide](../../testing/DEV8_CORE_ACCEPTANCE.zh-CN.md).

Source/compile/WARP are verified for this slice. NVIDIA NR, real game quality,
HDR/temporal stability, measured median/P95/P99 GPU cost and the 5% limit remain
unverified; hosted CI is reported separately. No visual/compatibility issue is
closed. Every safety bypass must retain its reason and request F8 BUG feedback;
F7 is needed only for a visual anomaly. Maintainer acceptance precedes main.
