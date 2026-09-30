# Self-hosted runner

The maintainer's Windows machine runs GitHub Actions jobs for this repository.
It is registered with the labels `self-hosted, Windows, X64, dlss5-build`; jobs
must use exactly `runs-on: [self-hosted, Windows, X64, dlss5-build]`.

## Jobs (in `.github/workflows/pull-request-validation.yml`)

| Job | When it runs | What it does |
|---|---|---|
| `self-hosted-tests` | Every PR to `main` (open, push, reopen) and every manual dispatch | `python scripts/run-portable-tests.py --verbose` with the mingw `clang++` when `DLSS5_TOOLCHAIN_ROOT` is known |
| `self-hosted-canonical-build` | Manual dispatch with **full_build** ticked, or a PR to `main` labelled **`full-build`** (runs again on each push while the label stays) | `scripts/ci/run-self-hosted-build.ps1` → `scripts/build-unified-candidate.ps1` with every canonical gate |

Both jobs are skipped unless the repository variable `ENABLE_SELF_HOSTED_CI` is
`true` (Settings → Secrets and variables → Actions → Variables). Turn it off when
the machine is offline or busy so PRs do not wait for the runner. Hosted jobs stay
behind `ENABLE_HOSTED_CI` and are unaffected.

## Runner tools

The runner machine has Windows PowerShell 5.1 and no PowerShell 7, so the steps
use `shell: powershell`; scripts they call must stay 5.1-compatible. The runner
service does not necessarily see the interactive PATH. The first step of each job
finds `git.exe` and `python.exe` on PATH, in the directory named by the optional
repository variable (or machine environment variable) `DLSS5_GIT_DIR` /
`DLSS5_PYTHON_DIR`, or in the usual install folders, and adds it to PATH before
checkout. Without git, `actions/checkout` downloads an archive with no `.git`
and the canonical build cannot record its source commit.

## Build inputs

`run-self-hosted-build.ps1` reads each input from a repository variable of the
same name; if that is empty it uses the runner machine's environment variable.
No machine path is stored in the workflow.

| Name | Required | Meaning |
|---|---|---|
| `DLSS5_UPSTREAM_ROOT` | yes | Pinned upstream carrier checkout (`-UpstreamRoot`) |
| `DLSS5_TOOLCHAIN_ROOT` | yes | llvm-mingw root (`-ToolchainRoot`) |
| `DLSS5_RESHADE_SOURCE_ROOT` | yes | ReShade source (`-ReShadeSourceRoot`) |
| `DLSS5_DIRECTXMATH_ROOT` | yes | DirectXMath (`-DirectXMathRoot`) |
| `DLSS5_FACE_RUNTIME_ROOT` | yes | ONNX Runtime for the face backend (`-FaceRuntimeRoot`) |
| `DLSS5_FFX_SDK_ROOT` | yes | FidelityFX SDK (`-FfxSdkRoot`) |
| `DLSS5_FFX_CACHE_ROOT` | yes | Verified FFX object cache (`-FfxCacheRoot`) |
| `DLSS5_BUILD_ROOT` | yes | Parent folder; outputs go to `<root>\ci\ci-<run>-<attempt>` |
| `DLSS5_NRCHAIN_REUSE_ROOT` | no | `-NrchainReuseRoot` |
| `DLSS5_CODEC_CACHE_ROOT` | no | `-CodecCacheRoot` (speed only) |
| `DLSS5_ALLOW_MISSING_DEBUG_LAYER` | no | Anything but `false` passes `-AllowMissingDebugLayer` (the current machine has no D3D12 debug layer) |

The values match the arguments in `docs/handoff/BETA13_NEW_PC.zh-CN.md`.
Check a machine without building: `./scripts/ci/run-self-hosted-build.ps1 -CheckInputsOnly`.

After a successful build the three newest `ci-*` output folders are kept and
older ones are deleted; a failed build deletes nothing. The log prints the SHA-256
of `source/source-manifest.json` and `validation.json`.

## Boundaries

A green self-hosted job is local Windows CPU/WARP evidence for that commit. It is
not hosted CI, real NGX inference or game acceptance, and it does not install
anything into a game. Record results per SHA as before.
