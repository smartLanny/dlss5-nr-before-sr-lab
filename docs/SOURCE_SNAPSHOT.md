# Source snapshot

This staging tree was prepared from the locally reviewed source snapshot on
2026-09-04. Hashes are SHA-256 and identify the files that should be compared
before a private-repository push:

| File | SHA-256 |
| --- | --- |
| `src/nr_before_sr.cpp` | `106EAA45E66AB6D98ECE85A7C6443B7B7126B7B9ADED0307E150ACE1A63B54F6` |
| `src/nrchain_nvngx.cpp` | `DD522FAAE5F24EE888792FA806BC438F1063F80F0A62EF54A0F50F40B3216049` |
| `config/nr_before_sr.ini` | `39CB6D4CD0D91B640E17ADF4EAF6BC16C79B2F699E090F572AE804E2C80A7F59` |
| `scripts/validate-codec-shader.ps1` | `4567508FBB9204CC7BE010D87910F1BD2679563AAE6193829F7E0D21748CBF76` |

The repository intentionally omits compiled add-ons, runtime DLLs, vendor SDK
files, model payloads, game files, screenshots, crash dumps, and raw logs. The
bounded observations in `TEST_EVIDENCE.md` are manually rewritten summaries,
not copied machine logs.
