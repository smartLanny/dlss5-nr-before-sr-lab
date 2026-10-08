# Default RR-post private-harness review subset

This is an own-source review subset, **not a portable host or a complete actual-EXE source package**. No production Core source changed. The full host depends on separately sealed full-Core objects, bootstrap/runtime owners and helper fixtures; these are deliberately absent. `REVIEW_ONLY_CORE_INCLUDE` is a review placeholder, not a build path.

Actual outcome: only the strict master-off arm ran once and stopped with exit 41 at scene command-state capture before native parent Create. Bootstrap/capability/official RR optimal passed, but SR/RR/F18 Create/Eval and scene Dispatch were zero. NR-on was not run. This is **unsupported harness command-state-envelope admission**, not product NR failure, a default-route pass, or game acceptance. No GPU retry or master-off architecture workaround followed.

The real list was registered, but the first capture reported hooks unavailable and an unobserved binding/reset baseline. The renderer-only resource observer does not provide the missing full command-state snapshot and does not supply production source evidence. Earlier research NR-off kept a CPU observer enabled and is not a strict master-off proof.

CPU narrow control: 13 exact extracted renderer batch checks; old r1 compile=0/run=41 with 7 missed unknown-batch refusals, fixed r2 compile=0/run=0 with all 13 retained. Dummy resource/list identities and a no-op native forwarder test rejection/forwarding only; no device, physical state, NGX or death proof. The actual GPU failure occurred earlier than the entry this change intended to test.

| Review file | Private source SHA256 | Public LF copy SHA256 |
|---|---|---|
| `post_rr_host.inl` | `fe7b5998081055c524c35bb39634816b753c681c995e98a61613262180d03689` | `fe7b5998081055c524c35bb39634816b753c681c995e98a61613262180d03689` |
| `post_rr_main.inl` | `7269eb89600f09ec09d529f323dfb7774b22c9adfa1a3600d5a22579629c3216` | `7269eb89600f09ec09d529f323dfb7774b22c9adfa1a3600d5a22579629c3216` |
| `renderer_state.inl` | `04a43c33209083027471608e72a576ab4b792807faa4c3dffd4707c09e7d91d1` | `04a43c33209083027471608e72a576ab4b792807faa4c3dffd4707c09e7d91d1` |
| `critical.diff` | `a7ea605dec4288bc929efde78d05e0aabaa4a9a5a8e0476c408b663e9030e0fe` | `034562bd20f3311b26016610095ef739dc8196d01865e785f785c46a74867ff7` |

The private r1 critical diff SHA256 is `9786502415abcc50d370536b733ecb58ce7bfd912acc956f745843c5056a815d`; the private r1→r2 narrow diff SHA256 is `4a57132b354ffadb1a3aad2da971ca45e92008986096ee189c5a9d2bd912fc5b`. Public `critical.diff` combines the full owned measure-r2→default-r2 changes rather than mislabelling that narrow patch as the full source delta. Original private sources/diffs/failed receipts were not modified. The review patch is generated with zero context from the same sanitized old/new text; hunk bodies are not whitespace-stripped. LF normalization, relative labels and the include placeholder are the only source-content transforms. The original private context-bearing diffs remain unchanged.

No NVIDIA SDK/vendor implementation, runtime/model/binary, raw log, credential, machine path or game file is included. Source uses project-owned probe code and public API names only. No claim that this subset reconstructs the actual EXE.
