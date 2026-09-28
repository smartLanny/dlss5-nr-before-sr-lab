# 0.5.2 Beta 9: admission under severe commit pressure

A focused fix after Beta 8. When native RR was enabled in NTE, the subsequent 4K NR creation threw std::bad_alloc with only about 75 MiB of available commit. Native RR had already been created successfully. Windows had also reported commit pressure before the switch.

- Check available process commit immediately before a new or replacement NR model is created. A valid reading below 1 GiB defers creation and retains native SR/RR output.
- Report commit-headroom-pressure separately and reuse the 300-frame and configuration-change retry paths. A declined attempt is not counted as an NR call or a vendor creation failure.
- Existing models, disabled paths and retry cooldowns perform no additional memory queries, GPU processing or waits.

The 1 GiB floor identifies severe pressure; it does not estimate opaque model allocation or guarantee that all allocation failures are prevented. The cause of the earlier commit growth remains unresolved. Pagefile settings, runtime DLLs and saved user settings are unchanged.

Beta 8 observation fixes and existing experimental options are retained. Ordinary balanced reconstruction remains recommended. Private SR and current temporal modes remain separate comparisons. The new spatial four-tap candidate failed its thin-line gate and remains outside the product. Post-RR native motion vectors, private Feature13 and stacked private-SR/external-history processing remain excluded.

This is a local maintainer candidate; accompanying receipts identify the checks performed. Controlled checks do not establish NTE game stability, game image quality or a whole-frame overhead of at most 3%. Beta 8 and its original failure evidence are preserved.
