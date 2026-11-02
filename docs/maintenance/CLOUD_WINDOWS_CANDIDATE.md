# Cloud Windows candidate validation

The **Mainline validation** workflow can build the committed development Core
without a local Windows machine. The version and exact source SHA come from the
checked-out source and the canonical receipts, never from this document.

## Request a run

For a public, owner-authored PR from this repository to `main`, the repository
owner applies **`cloud-windows-build`**. Opening or updating that labelled PR also
requests validation of its current head. A label event for any other label does
not request this Windows job. Alternatively, the owner manually dispatches
`pull-request-validation.yml` with **`cloud_windows_build: true`**.

This is independent of `ENABLE_HOSTED_CI` and the existing self-hosted
`full_build` / `full-build` path. Private repositories, fork PRs, non-owner PR
authors and non-owner actors cannot enter the new hosted route. The build uses
the standard `windows-2022` image with read-only repository permissions and no
inherited secrets. No larger runner or automatic push trigger is used.

[GitHub documents standard hosted runners as free for public repositories](https://docs.github.com/en/actions/concepts/billing-and-usage).
The [standard runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
lists the Windows resources. Organization policies, service availability and
capacity still apply; a queued, skipped, cancelled or failed run is not a pass.

## What actually runs

The reusable `compat-post-dev2.yml` job prepares pinned public LLVM-MinGW,
ReShade, DirectXMath, carrier, NVIDIA NGX SDK, ONNX Runtime, YuNet and FidelityFX
inputs. SDK and model archives are hash checked. Git inputs use full commit
pins; the original canonical builders verify their source/SDK hashes again.
The official NGX static SDK library builds this project's `nrchain_nvngx.dll`;
NVIDIA runtime/model directories are not checked out or redistributed.

`scripts/build-unified-candidate.ps1` runs in full, including the actual SM5
shader compilation, Windows CPU contracts, positive and negative WARP tests,
exact staged Core and bilingual builds. The complete face backend and the
compiled FidelityFX SDK/cache are mandatory. No `AllowMissingDebugLayer`,
compile-only replacement, stub binary or gate suppression is used. Face tracker
WARP, the real ONNX CPU worker and the additional temporal consumer checks run
before packaging. A missing dependency or failed gate stops candidate upload.

This is Windows software and WARP validation. It does **not** establish NVIDIA
Feature18/NGX inference, hardware AMD optical flow, game acceptance, whole-frame
performance, image quality on a GPU, or compatibility with every game. The
existing experimental defaults and saved configuration behavior are unchanged.

## Artifacts and installation scope

- **Core-only candidate:** separate `zh-CN` and `en` OTA ZIPs, package receipts
  and SHA256 sums. Each contains one Core, its matching project-built nrchain,
  manifest, instructions and licenses. The existing packager verifies the
  canonical/postbuild receipts, frozen source, PE identity and binary hashes.
- **Windows evidence:** JSON receipts and text/console logs are retained even
  after a failed step. A failed build may have partial receipts; only a fully
  successful job produces the candidate artifact. SDKs, objects, executables,
  raw captures and vendor payloads are excluded from evidence upload.

Artifacts expire after seven days. The packager limits each uncompressed OTA
payload to less than 128 MiB; typical ZIPs are much smaller. Exact upload sizes
come from the run. Download a candidate only when its successful source SHA is
the version you intend to test.

These are optional development candidates, with no Release publication or
Manager pin change. For a later manual test, exit the game, back up the old Core
and nrchain outside plugin search paths and keep exactly one language Core.
Preserve the INI, model settings, existing NR/SR/RR/FG runtimes and face assets.
The OTA does not supply those resources or enable experiments. Retain the prior
tested package for rollback. Windows/WARP evidence cannot replace that later
game check.

Initial dependency preparation and the complete canonical gate can take well
over an hour. Each Windows job is capped at 180 minutes. The first actual hosted
run, rather than a source-only workflow test, establishes whether the selected
runner image satisfies every required runtime capability.
