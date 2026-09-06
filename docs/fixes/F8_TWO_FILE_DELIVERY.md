# F8 two-file delivery (#47)

Owner's NBA2K25/e5ce121 capture contained `feedback-report.txt`, `feedback.zip`
and three `summary-N.txt` siblings. Each summary was byte-identical to its
`logs/*-summary.txt` ZIP entry and appeared in the full TXT. The report was
present; duplicate intermediate files were not cleaned up. This was not a
player error or evidence that the local agent compiled the older candidate.

After both a COMPLETE archive and complete persisted TXT exist, remove only
included, unchanged, untruncated summaries whose canonical ZIP name and exact
current-session `summary-0..4.txt` source match. Recheck content SHA256 through
an existing-file handle with delete access. Deny write/delete sharing; reject
reparse points, directories, multiple hard links and size/hash differences.
Mark that same open handle for deletion, never reopen a verified path to delete.
No directory scan, recursive cleanup, original-log deletion or prior-session
cleanup is introduced.

Partial/failed captures, TXT write failures and locked/changed scratch keep
recovery files. Cleanup is best effort and does not change the already-written
report's status. The TXT explicitly says only TXT and ZIP are for submission.
F7, NR/SR state, GPU resources, user INI and runtime binaries are unchanged.

Validation: `python scripts/validate-feedback-delivery.py` compiles the actual
production read/cleanup functions and SHA256 with fault-injected Win32 IO.
`tests/feedback_delivery_win32.cpp` additionally runs the real ZIP/TXT writer
and real Win32 files, including a locked scratch, hard link, original log,
changed content and failed/incomplete text. Source/host tests are not a claim
of successful Windows CI or game verification; inspect actual PR checks.

NBA2K25 runtime failure is tracked separately in #127. It had SR calls but no
NR or submission, persistently awaiting a verified Reset. Removing duplicate
feedback files does not fix that game and must not close its issue. A missing
live command-state snapshot also remains distinct from the included log tail.
