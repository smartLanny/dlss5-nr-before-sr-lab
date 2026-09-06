# Cloud agent notes for issue #38

- Prefer **TAIL** of nr-*-summary / nr-before-sr after enable over early boot lines.
- `installing=1` and early `native-hooks-unavailable` are often **transient** during hook install — do **NOT** treat as final failure.
- Judge by post-enable trailing status, bypass reasons, Feature18, and submit counts.
- Local full zips stay off-repo; these files are the cloud-readable excerpts.

Source dirs: unpacked_NeroEggs_0.4.4test1_sinking, unpacked_NeroEggs_0.4.3r3_sinking
Files prepared: 0
