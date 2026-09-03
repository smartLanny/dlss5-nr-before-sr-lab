# Source snapshot

This staging tree was prepared from the locally reviewed source snapshot on
2026-09-03. Hashes are SHA-256 and identify the files that should be compared
before a private-repository push:

| File | SHA-256 |
| --- | --- |
| `src/nr_before_sr.cpp` | `2835CF5BE2D7BE675B1F7F094357629DED66C88CAC4F87DA91DB6BFC4CE5B4AB` |
| `src/nrchain_nvngx.cpp` | `DD522FAAE5F24EE888792FA806BC438F1063F80F0A62EF54A0F50F40B3216049` |
| `config/nr_before_sr.ini` | `D08C30C4F6895484E96ADA9A9A7319D3F0C6E17CDB946DB114080CCE9B4A1A41` |

The repository intentionally omits compiled add-ons, runtime DLLs, vendor SDK
files, model payloads, game files, screenshots, crash dumps, and raw logs. The
bounded observations in `TEST_EVIDENCE.md` are manually rewritten summaries,
not copied machine logs.
