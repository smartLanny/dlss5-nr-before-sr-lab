# RTX 40 community test track

This track tests the **same beta0.3 add-on build** used on RTX 50 hardware. It is
runtime-only compatibility work: no RTX 40-specific add-on fork should be
created, and no second ShortFuse/RHI NR add-on should be bundled or enabled.

## Distribution boundary

ShortFuse `310.8.SF-v2` is a community-modified, unsigned runtime. It is **not
bundled in this repository or in a public beta0.3 package until its license and
redistribution rights are confirmed**. The repository contains no NVIDIA or
ShortFuse DLL, model, weight, kernel, or download mirror.

Until permission is confirmed, maintainers may publish only:

- the beta0.3 add-on source/build manifest;
- a SHA-256 hash of the exact runtime tested;
- text instructions for selecting a lawfully obtained runtime locally; and
- sanitized test results that do not expose binaries or machine-specific data.

Do not rename, rehost, patch, or embed SF-v2 to make it appear project-owned.

## Test configuration

1. Use the current beta0.3 add-on and matching configuration.
2. Supply exactly one Feature 18 runtime from a lawful source outside this
   repository. Record its filename, byte size, SHA-256, and signature status.
3. Remove or disable any other DLSS5/NR ReShade or RHI add-on. ShortFuse may be
   used as the runtime only; running a second NR owner can cause duplicate work
   or silent bypass.
4. Start with `NR -> SR`. `SR -> NR` is the full-resolution comparison path and
   activates only when its live resource/color contract passes.
5. If Ray Reconstruction is active, expect `RR safe bypass` and zero Feature 18
   evaluations. This is correct behavior, not a compatibility failure.

## Qualification scope

The package name must remain **RTX 40 community test** until current-build tests
pass on at least RTX 4070 and RTX 4090D. A successful run on one GPU, game,
driver, or SF-v2 build must not be generalized to the whole RTX 40 family.

A useful report includes GPU model, driver, Windows build, game/build, add-on
commit, mode, render/output extents, texture formats, runtime hash/signature,
Feature 18 create/evaluate result, and the bypass reason. Remove usernames,
absolute paths, process addresses, account identifiers, screenshots containing
personal information, and proprietary files before sharing.

## Stop conditions

Stop testing and retain the native game result if the overlay reports runtime
rejection, lifecycle isolation, unsupported format/exposure, duplicate NR owner,
or an uncertain resource contract. A crash, device removal, non-null handle on a
failed create, or Feature 18 activity during master-off/RR bypass is a release
blocker and should be reported with a sanitized log and, when lawful, a minimal
call-order summary.
