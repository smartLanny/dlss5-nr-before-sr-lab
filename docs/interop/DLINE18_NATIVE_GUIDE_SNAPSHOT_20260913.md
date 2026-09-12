# D18 native guide capture follow-up

Base: D17 `7d5bef2f8ab3ee115ea8b806753655f8e6d8fc5f`. This candidate follows
the owner's supplied cloud patch (archive SHA256
`89fd248dd8138a46e36b184f7190350cefd3363b0a8d0dfd3852b3e94d5ba566`).
Private feedback logs and the archive are not included in the source export.

## Failure and change

The two owner reports reached the native producer but had no native guide
receipt; Present had not evaluated NR. NTE's depth allocation was 2228×1256
with an active 2228×1253 region. The previous depth-stencil copy required
physical and active sizes to match. Onimusha did not have that padding;
missing recording-local resource-state evidence remains a separate candidate
cause, which the new capture-stage diagnostics can identify.

- Copy the complete depth-stencil subresource into an equally sized owned
  texture and retain the active region, including its origin.
- At the genuine NGX reconstruction input boundary only, use typed shader
  loads to snapshot real depth/motion when resource-state observation is
  unavailable. NGX specifies NON_PIXEL_SHADER_RESOURCE for these inputs.
  Contradictory observed states still reject capture. No dummy guides or
  guessed transitions are introduced.
- Preserve motion precision and signed values. Clear predication for the
  private snapshot dispatch; restore the native command state afterwards.
  Preflight failures do not replay an untouched command-state envelope.
- Prefer free, consumed or expired receipt slots over fresh pending receipts.
  F8 now reports capture attempts/stages, dimensions and transport choice.

D17's submitted producer fence, device/extent/freshness checks, cross-queue
Wait, once-only consumption and known FG exclusions remain in place. The
new path does not require complete SR-to-backbuffer lineage. It does not
remove the output-size match requirement or infer it from monitor size.

## Bounded validation and next action

The targeted WARP test covers padded depth, nonzero depth/motion origins,
signed half motion, real shader dispatch, predication, unchanged inputs,
conflicting-state refusal and producer/consumer fence retirement. Its state
envelope is an admission fixture; it does not prove Core state restoration.
The controlled NVIDIA host separately enters actual CaptureNgxGuides and
the production Present NR path. Its source metadata and native-SR success
receipt are controlled inputs, not a real game's SR/FG implementation.

Final build identity, test results and hashes belong to the frozen package's
validation manifest. Game acceptance remains open: test compatibility mode
in a stable NTE/Onimusha scene, use F6 for comparison, and collect one F8
report per game if it remains ineffective. This batch does not reopen
multilayer resolution research or claim a complete FG/game regression pass.
