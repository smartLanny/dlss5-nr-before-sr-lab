# 0.4.3r2 candidate: native-output post-SR and feedback UX

## Baseline and scope

Based on the inspected version branch `codex/beta0.4.2`, commit
`0b5d04dddee42751827d39195c5df3809567d7f8`, after #48, #56 and #57 were merged
there. This is not a merge into `main` or a released/signed 0.4.3r2 package.
Existing build strings are not release authorization; identify test binaries by
source commit, dirty status and the measured addon SHA256.

## 玩家操作

界面保留顶部运行状态，设置拆成「画质」「反馈与报告」「诊断」三个页签。
日常只调画质；不生效或旁路先用 F8 核心小包。采集完成后可点击
「复制报告摘要」，粘贴到已有反馈渠道，再补充问题现象和复现步骤，并附本次 ZIP。

画面异常才用 F7。F7 会生成新的一对核心包和图像包，请提交这两个新包，
不要使用之前的 F8 核心包与新的图像包混配。部分完成的核心包仍然有价值。
游戏已崩溃或不能进入场景时，继续使用离线崩溃收集器；F8 不是 dump/异常栈替代品。

不自动上传，不打开账号登录，不扫描整个游戏目录，不删除或替换任何 DLL。
摘要不含本机绝对路径；提交前仍需自行检查内容。

## Local report generator

Requires Python 3.11+ and the adjacent production parsers. Analyze completed,
immutable archives; do not replace or modify them during inspection.

```sh
python scripts/feedback-report.py core.zip --output report.md
python scripts/feedback-report.py core.zip --image image.zip --symptom visual --output visual-report.md
python scripts/feedback-report.py core.zip --symptom crash
```

The output file must not exist: existing files are never overwritten. The tool
validates the core, and optional exact core/image pair, before producing Markdown.
It includes archive SHA256, bounded whitelisted identity/settings fields and
observed counter deltas. Counter resets are not reported as negative activity.
No files are extracted or uploaded. CRC/SHA agreement is consistency, not trusted
authorship, GPU completion or proof that NR is visibly working.

Historical log clues are explicitly labelled historical. A duplicate-ReShade
hint requires the reported initialization-abort wording, not just two DLL names.
Verify actual loaded paths and back up before manual troubleshooting; do not
blindly remove `dxgi.dll` or `d3d12.dll`. Null-PSO evidence does not authorize
turning off safety guards. Device-removed text does not by itself identify a
driver root cause.

## Post-SR design

Previously packed outputs used the original SR target, while FP16 outputs could
be redirected to a private active-size intermediate before native SR. The new
FP16 route also lets native SR write the game's original Output and original
subrect. Only after successful native SR does it copy the active rectangle into
the existing private FP16 intermediate and evaluate NR.

`CopyPostSrFp16Snapshot` is the production copy routine. It checks non-null and
distinct resources, typed FP16 single-plane/single-mip/single-array/sample-count-1
textures, nonzero extents and overflow-safe source/destination bounds before
recording commands. Original Output enters as UAV by the existing NGX boundary
contract, transitions to COPY_SOURCE, then returns to UAV. The private destination
transitions to COPY_DEST and then NON_PIXEL_SHADER_RESOURCE. The copy keeps RGBA
bit patterns and uses the actual source base, writing at private origin zero.

No SRV of native FP16 is created, so native FP16 with DENY_SHADER_RESOURCE keeps
its existing admission path. Packed R11, profiled R8 and admitted PQ R10 continue
to use the existing typed-view/codec/exposure path; no format or HDR heuristic is
added. All same-device, resource ownership, workset, queue, recorded-list and
exact-fence admission remains the caller's existing responsibility.

The post-SR finish path now also honors `suppress_transfer_this_frame`. This
prevents a reset/guide/exposure discontinuity frame from being blended when
Warm-up is set to zero. Identity, ordinary warm-up and unsuccessful NR retain
the native SR picture. This is a confirmed source-level gap, not proof of a
specific game's flicker root cause.

This is full-size Mode 1, not low-resolution post-SR, synthetic guides, a new
D3D11 provider, or a generic bypass for unknown command state. #12's broader
low-resolution post-SR work is not completed here. RR/Feature13 and competing NR
ownership guards remain unchanged.

### Cost and risks

FP16 adds an active-rectangle GPU copy versus redirecting native SR. It reuses
the existing intermediate rather than adding another image, but memory bandwidth
and real frame-time impact have not been measured. The change deliberately
prioritizes preserving the original SR contract over claiming a speed gain.
Resource lifetime still relies on the existing exact-list/fence workset tracker.
WARP cannot verify vendor NGX internals, hardware/driver behavior or game inputs.

## UI and feedback consistency

The three tabs use stable language-independent IDs and the existing independent
overlay; they do not change another addon's layout, font atlas or focus.
The final strength control uses the actual `ActiveTransferStrength` function.
The production v4 implementation intentionally shares final strength across
modes and normalizes the legacy post field; this PR does not reintroduce two
independent strengths or claim that legacy-field mismatch was a live UI bug.

A report summary is built from the completed core session, not current live
settings. A fresh request clears the old ticket. Failed captures never expose
an old ticket. Status, ZIP path, reason and ticket are copied together under the
same publication lock, then rendered/copied to the clipboard after unlocking.
First duplicate-ReShade and device-failure clues now survive later log noise
within the existing bounded summary budgets.

## Verification

Host validation in preparation run 33971926007 passed 52 production UI/policy/
ticket/summary checks, 12 report tests, 32 core parser tests, 18 image parser tests
and the existing six bug-regression groups. These checks do not mean game runtime
validation. The first draft test assumed separate mode strengths; inspecting the
actual unified helper corrected that test assumption without changing semantics.

The PR workflow also runs the actual Windows core coordinator/IO/ZIP tests,
shader and existing resource/lifecycle/codec WARP checks, LLVM analysis, and
English/Chinese addon builds. A new WARP-only test calls the production FP16 copy
with padded/nonzero rectangles, repeated worksets, denied shader reads, RGBA bit
patterns, bounds rejection and reset/warm-up policy. A wrong expected pixel must
be rejected as a negative control. Refer to the exact final PR checks for their
status, not this planned validation description.

Run host checks:

```sh
python scripts/validate-043r2.py --compiler g++
python -m unittest discover -s tests -p test_feedback_report.py -v
```

## Real-game acceptance still required

| Area | Required check |
| --- | --- |
| Native SR | Master off, NR creation/evaluation failure, identity mode, RR pass-through; original output and parameters preserved |
| Post-SR | FP16 with and without shader-read access, nonzero subrect/padded storage, repeated frames and output rotation |
| History | Alt-Tab, scene/load/DRS/HDR/exposure changes at Warm-up 0 and 3; no unsafe first-frame blend |
| Packed formats | Existing R11, profiled R8 and known PQ path remain correct; unknown encoding remains bypassed |
| Lifetime | Multiple queues/swapchains, sustained frames, bounded workset retirement, no fence/lifetime bypass |
| UX | Chinese font fallback, EN/ZH, DPI, RenoDX coexistence, tabs during pending edits, F8/F7/cancel/partial results |
| Reports | Exact binary/session, current-versus-historical clues, no stale report/path, manual upload only |

#37 is a candidate regression target for post-reset behavior, not a confirmed
fix for all underexposure/flicker. #39/#42 need their own visual A/B evidence.
#49 crash and #38/#50/#53/#58 command-state gaps remain independent. #59's
initialization conflict gets better evidence preservation, not automatic repair.
Keep related issues open until their actual acceptance criteria are met.
