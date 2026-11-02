# F8 TXT report input — 2026-11-03

## Problem and change

The live F8 collector writes one `DLSS5-CoreText-v2` TXT using
`nr_feedback_single::Document`; `feedback-report.py` still accepted only the
older core ZIP. A normal current F8 file therefore failed at the report entry.

`scripts/parse-core-text.py` now reads the original TXT framing, validates each
included summary's exact byte count and SHA256, and exposes only explicitly
requested capture fields to the existing local Markdown report. Summary bytes
are consumed by their declared length, so a historical log line that resembles
`end.nrSuccess`, a section heading or a completion marker cannot replace the
current capture metadata. Repeated trace-window records remain valid.

The inspector is bounded to 2 MiB and additionally enforces the document's own
`text.limitBytes` and each 24 KiB summary bound. This accepts original 480 KiB
documents and the expanded current writer without making its declared budget
optional. It rejects duplicate current scalar fields, altered summary hashes,
unfinished files, false completeness, unexpected side effects and trailing data.

Explicit partial captures remain usable: missing required logs, incomplete
scans, and a summary omitted by the document byte budget do not become complete.
Optional log absence is separate from required evidence completeness. Unknown
metadata, raw excerpts and input paths are not copied into the report. Existing
report-field sanitization remains in place.

An F8 TXT has no ZIP manifest and cannot prove a core/image pair. `--image`
therefore still requires the new core ZIP produced by the same F7 collection.
Byte consistency is not trusted authorship, visible image quality or GPU
completion. Files whose line endings were rewritten are not original byte
evidence; use the original game-generated TXT.

## Verification

Commands on Linux, GCC 13.3:

```sh
CXX=g++ python3 tests/test_core_text_feedback.py
python3 tests/test_feedback_report.py
git diff --check
```

- 19 TXT tests pass. The fixture compiles the actual production `Document` and
  `Summary` headers; OS collection and capture metadata are explicit fixtures.
- 15 existing ZIP report tests pass, including pairing, hash failures, privacy,
  counter reset, and create-new-only output behavior.
- Negative inputs cover hash/byte-count corruption, duplicate sections/scalars,
  footer truncation, extra bytes, CRLF rewriting, exceeded budgets, false
  completeness, and TXT/image pairing attempts.
- No new GPU, Windows filesystem, NGX or game result is claimed by these tests.

## Use

```sh
python scripts/feedback-report.py feedback.txt --output report.md
python scripts/feedback-report.py feedback.txt --symptom visual --output visual-report.md
python scripts/feedback-report.py core.zip --image images.zip --output image-report.md
```

The output file must be new. The command does not upload files, overwrite the
input, change settings or touch a game installation.
