# Final effect strength limited to 4 (unreleased)

Refs #102. The public final blend slider changes from 0–8 to 0–4 in both processing modes. Existing TransferStrength values above 4 normalize to 4, and PostTransferStrength stays synchronized; values 0–4 and master-off are unchanged. The current new-install and recommended default is 1.0. Nonfinite requests retain the existing safe-zero behavior.

This is **final effect strength**, not the model Intensity control (still 0–2). No hidden >4 override is added. UI, configuration comments and diagnostic screenshot naming share the same cap. This does not claim that 4 is a recommended setting or guarantee colour fidelity; the 0.5 high-strength protection and #103 colour-follow research remain separate.

Tests compile the actual production normalization statements, reject the historical 8x implementation, and check UI/INI/filename/default wiring. Runtime INI write permissions and visual quality still need normal use validation. Existing complete CI must remain green; no shader/NR resolution/MV/jitter/HDR/FG algorithm change is included. This PR does not create or replace a Release, and should later be forward-synced into next/0.5 through the canonical maintenance flow.
