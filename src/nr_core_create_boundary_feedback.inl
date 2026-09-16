// Included in FeedbackAppendActivity. Anonymous counts, no module paths/addresses.
FeedbackAppend(s,
    "%s.coreEntry.schema=1\n%s.coreEntry.resolverReady=%d\n%s.coreEntry.installStatus=%d\n"
    "%s.coreEntry.resolutions=%llu\n%s.coreEntry.createEntries=%llu\n"
    "%s.coreEntry.coveredBeforeCreate=%llu\n%s.coreEntry.uncoveredBeforeCreate=%llu\n"
    "%s.coreEntry.targetConflicts=%llu\n%s.coreEntry.pinFailures=%llu\n"
    "%s.coreEntry.policy=typed-native-Create-boundary;no-synthetic-handles;no-fault-reset\n",
    prefix, prefix, g_core_boundary_hook_ready.load(std::memory_order_relaxed) ? 1 : 0,
    prefix, g_core_boundary_install_status.load(std::memory_order_relaxed),
    prefix, g_core_boundary_resolutions.load(std::memory_order_relaxed),
    prefix, g_core_boundary_entries.load(std::memory_order_relaxed),
    prefix, g_core_boundary_covered.load(std::memory_order_relaxed),
    prefix, g_core_boundary_uncovered.load(std::memory_order_relaxed),
    prefix, g_core_boundary_conflicts.load(std::memory_order_relaxed),
    prefix, g_core_boundary_pin_failures.load(std::memory_order_relaxed), prefix);
