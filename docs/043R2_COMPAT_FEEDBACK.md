# 0.4.3r2 candidate: native-output post-SR and feedback UX

## Baseline and scope

Initial baseline was `0b5d04d` after #48/#56/#57. The development branch now also
includes `e2a3fba` / #60's beta0.4.3 release identity, without reverting its runtime,
build, packaging or feedback documentation updates. The target version branch
remains named `codex/beta0.4.2`. This is an r2 candidate, not a signed/released r2
package or a merge into main. Match test binaries by source SHA, dirty status and
actual addon SHA256; final r2 release identity is a separate freeze step.

## 玩家操作

顶部保留实时运行状态，正文分成「画质」「反馈与报告」「诊断」三个页签。
不生效或旁路先采 F8 核心小包，完成后点击「复制报告摘要」，粘贴后补充现象
和复现步骤，并附同一次核心 ZIP。颜色、亮度、闪烁等画面异常才用 F7；
F7 会生成新的一对核心/图像包，请附新的一对，不要混用旧 F8 核心包。
部分完成的核心包仍有价值。游戏不能启动或已崩溃时继续使用离线收集器；
F8 不是 dump/异常栈的替代品。

不自动上传，不打开账号登录，不扫描整个游戏目录，也不删除/替换 DLL。
摘要不含本机绝对路径；提交前仍应检查内容。

## Local report generator

Requires Python 3.11+ and both adjacent production parsers. Use completed,
immutable ZIPs; do not replace them during inspection.

```sh
python scripts/feedback-report.py core.zip --output report.md
python scripts/feedback-report.py core.zip --image image.zip --symptom visual --output visual-report.md
python scripts/feedback-report.py core.zip --symptom crash
```

The output path must not already exist. The tool validates core/optional exact
binary pair before producing Markdown, including archive SHA256, whitelisted
metadata and valid monotonic counter deltas. Counter resets remain unknown.
No extraction/network/upload occurs. CRC/SHA consistency is not authorship,
GPU completion or working-NR proof. Historical log clues are labelled historical;
two DLL names alone are not evidence of an initialization abort. Inspect loaded
paths and back up before troubleshooting; never blindly delete dxgi/d3d12 DLLs.

## Post-SR implementation

Native SR always keeps the game's original Output and subrect. After native SR
succeeds, a CopyTextureRegion copies the active FP16 rectangle to the existing
private intermediate; NR runs against that snapshot. Packed R11/R8/known PQ
formats continue through their existing shader conversion and color contracts.
No new unknown-format or HDR admission is added.

The production helper `CopyPostSrFp16Snapshot` checks distinct resources, typed
FP16 2D/color/UAV flags, single mip/array/sample, nonzero extents and overflow-safe
bounds before recording commands. The original UAV becomes COPY_SOURCE then UAV;
the private image becomes COPY_DEST then NON_PIXEL_SHADER_RESOURCE. RGBA bits,
source subrect origin and untouched outside pixels are tested explicitly. No
SRV is created from the native FP16 image, but that does NOT make invalid resource
flags legal. DENY_SHADER_RESOURCE requires ALLOW_DEPTH_STENCIL and is not a valid
flag combination for this FP16 color UAV path. The helper rejects such flags.

Microsoft reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion

The first WARP attempt used an invalid DENY_SHADER_RESOURCE FP16 color fixture
and was correctly rejected during resource creation. The final harness keeps it
as an explicit negative resource-creation test, not an advertised supported case;
all copy/pixel/bounds assertions remain. Valid cases use legal color textures.

`FinishAfterSr` now honors `suppress_transfer_this_frame`, so a reset/guide/exposure
history discontinuity cannot blend NR just because Warm-up=0. Failed NR, identity,
warm-up and reset-suppressed frames preserve native SR. This is a source-level
gap fix, not proof of a particular reported game's root cause.

Same-device ownership, resource references, original command-state restoration,
workset and exact submission fence remain the existing caller's responsibility.
RR/Feature13, competing NR ownership and null-PSO safety guards are unchanged.
There are no new shaders, resolution modes, jitter/guide remappings, DX11 bridge,
FG/MFG hooks or synthetic-guide inputs in this PR. #12 low-resolution post-SR is
not completed: this remains full-resolution Mode 1.

### Tradeoff

FP16 adds one active-region GPU copy compared with redirecting native SR, reusing
an existing image but consuming bandwidth. No measured performance claim is made.
Preserving native output compatibility takes priority; real-game frame times and
long-run queue/resource retirement still require verification.

## UI and publication

Tabs use stable language-independent IDs under the existing independent overlay.
No shared font atlas, other window position or focus is changed. The slider uses
actual ActiveTransferStrength; v4 intentionally shares final strength across modes
and normalizes the dormant legacy post field. An early test assumed separate
strengths and was corrected after inspecting the real helper; this was not a live
UI bug or an opportunity to silently introduce new parameter semantics.

The report ticket is generated from the completed core session, not live settings.
New requests clear old tickets; failure never exposes a prior ticket. Status,
path, reason and ticket are read under one publication lock, with drawing and
clipboard work outside the lock. Duplicate-ReShade aborts and device-failure
clues survive later noise within existing summary limits. When path redaction
would remove the actual abort clause, only a fixed marker is preserved; removed
path text is never reconstructed.

## Verification and limits

Host staging run 33971926007 passed 52 production UI/policy/ticket/summary checks,
12 report tests, 32 core parser tests, 18 existing image parser tests and six
existing bug-regression groups. Subsequent updates add privacy/abort checks.
Full PR checks cover actual Windows core coordinator/file IO/ZIP generation,
WARP copying and wrong-pixel negative control, existing codec/command-state WARP,
shader compilation, LLVM analysis and English/Chinese builds. Read the exact final
head's Checks for results; this document does not predeclare success.

```sh
python scripts/validate-043r2.py --compiler g++
python -m unittest discover -s tests -p test_feedback_report.py -v
```

WARP is a software adapter, not NVIDIA NGX/model or real-game verification.

| Real-game acceptance | Required coverage |
| --- | --- |
| Safe fallback | Master off, NR create/evaluate failure, identity and RR pass-through |
| Post-SR copy | Legal FP16, nonzero/padded output rectangles, repeated and rotating outputs |
| History | Alt-Tab/load/DRS/HDR/exposure changes at Warm-up 0 and default |
| Packed formats | Existing R11/profiled R8/known PQ behavior, unknown encoding still bypassed |
| Lifetime | Multi-queue/swapchain, sustained workset retirement, exact fences |
| UX | EN/ZH/font fallback/DPI, RenoDX coexistence, tabs and pending edits, F8/F7/cancel/partial |
| Evidence | Exact session/binary, no stale paths/tickets, historical clues not called current root cause |

#37's reported flicker is PRE-SR; the new POST-SR guard cannot be called its
confirmed fix. Its post-mode underexposure also requires separate A/B evidence.
#39/#42 visual regressions, #49 crash and #38/#50/#53/#58 command-state gaps remain
separate. #59 receives evidence preservation, not automatic installation repair.
Keep game issues open until their real acceptance criteria are met.
