# Cloud agent notes — issue #101

## How to read
1. Start with `*-capture-info.txt` / `*-manifest.json` and **TAIL** of `*nr-*-summary*` / nr logs (post-enable).
2. Then open full `*nr-*-summary*` and `*reshade*` text files in this folder for complete evidence.
3. Do **NOT** treat early-boot `installing=1` or first `native-hooks-unavailable` as final failure — often transient during hook install. Judge by trailing status, bypass reasons, Feature18, submit counts.

## Scope
Full **text** logs only (no images/zip). Sources: unpacked_wang_feedback_small
Files: 5
