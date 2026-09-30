# Beta 13 source transformation archive

These 16 `patch-*.py` scripts and their former prepare/build entries are historical
evidence. The canonical build no longer imports or executes them. Recover the
original checkout at `cd8ba7009d333f7b03c463dc4237a913f9913081` to reproduce that
generator; the archived files retain their original bytes and relative-path
assumptions, so this directory is not a runnable alternate release line.

Current builds export committed `src/` directly through
[`prepare-core-source.py`](../../prepare-core-source.py). See
[`SOURCE_LAYOUT.md`](../../../docs/maintenance/SOURCE_LAYOUT.md) for the migration
proof and the pinned external carrier overlay.

旧脚本仅供查证历史，不参与当前构建。修改 Core 应直接改正式源码；不要恢复补丁链或
重新填入源码哈希。重现旧构建时请检出原始提交，保留当时依赖与回执。
