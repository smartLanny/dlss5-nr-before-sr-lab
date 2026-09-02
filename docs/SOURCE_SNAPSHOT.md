# Source snapshot

This staging tree was prepared from the locally reviewed source snapshot on
2026-09-02. Hashes are SHA-256 and identify the files that should be compared
before a private-repository push:

| File | SHA-256 |
| --- | --- |
| `src/nr_before_sr.cpp` | `8B81DBBEE268AFB73201A8A602942A93A5E363CDDDD1883AA83D080F4F996F39` |
| `src/nrchain_nvngx.cpp` | `DD522FAAE5F24EE888792FA806BC438F1063F80F0A62EF54A0F50F40B3216049` |
| `config/nr_before_sr.ini` | `3A37308FDAC26363947DF611C814538979494632335C7A7092D0FE67FD1D1910` |

The repository intentionally omits compiled add-ons, runtime DLLs, vendor SDK
files, model payloads, game files, screenshots, crash dumps, and raw logs. The
bounded observations in `TEST_EVIDENCE.md` are manually rewritten summaries,
not copied machine logs.
