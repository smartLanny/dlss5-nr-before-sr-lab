# 0.5.2 Beta 10: complete the low-commit guard for additional NR layers

Fix an omission in Beta 9: independent additional NR layers could bypass the severe commit-pressure guard. Under low available commit, defer that layer before registering its work or entering the vendor Create call, preserving already recorded primary work. Retry using the parent-frame counter or a configuration change, without claiming adoption of an incomplete chain. Existing models and disabled paths add no headroom query. Real opaque failures retain their safety isolation.

Also avoid unnecessary model recreation when the original color texture switches between R11G11B10_FLOAT and FP16 while NR still receives FP16 input/output under the same codec contract. Preserve the model but reset history. This exception applies only to that float-format pair; slots still require safe format, identity and completion handling, and all other contracts remain strict.

The 1 GiB floor is neither a model allocation estimate nor a guarantee against all OOM failures. The observed Beta 9 vendor error was PlatformError under low commit; its internal failing allocation is not independently proven. NR briefly disappears during the Esc opening animation and returns when the menu is fully open. Its cause remains unlocalized. Existing recreation logs now include changed guide formats, bound extents and conversion flags without relaxing admission. A same-process F8 immediately after the animation and its timestamp are still needed; this is not claimed as fixed.

Retain the existing features relative to 0.5 D21 and the Beta 8/9 fixes for real SR/RR timing, actual route reporting, primary RR bindings, matching texture reuse, safe residency recovery after persistent layer reduction, and bounded pipeline caching. Ordinary balanced reconstruction remains recommended. Experiments remain independent; disabled image experiments create no resources or GPU work of their own. Saved settings and runtime DLLs are preserved.

Both post-restart Beta 9 game recordings contain five groups with 600 valid local GPU samples each. The three temporal groups still lack adoption. General private-SR quality gains, denoising, trail removal and whole-frame cost remain unproven. Post-RR native MV, private Feature13 and stacked private SR/external history remain excluded, as does the rejected four-tap spatial candidate.

Build, hardware and deployment receipts identify this local maintainer candidate. Keep the old Core rollback. No game launch, online OTA publication or Manager default change is performed.
