# 0.2 layering

Desktop: main/preload IPC -> app-service -> schema-aware nr-config + installer -> product-owned transaction journal.

Native: ReShade public callbacks -> Quickbar ImGui view -> versioned NRUI C ABI -> core ApplyConfig. The sensor provider is a separate low-rate worker. The GUI process is not needed while gaming.

Reused upstream: local library discovery, PE/API scanning and process/anti-cheat guards via the pinned DLSS5-Swapper submodule. Product installation no longer shares upstream's pending-switch.json. Copying or restoring outside managed fixed filenames is refused.

Games running: change parameters only through the C ABI in the game's UI. Desktop INI editing is game-closed only. Runtime measurements and static installed-file checks remain separate.

Extension points: alternate telemetry provider (e.g. a verified PresentMon adapter), additional explicitly tested rendering routes, game compatibility catalogue, new core ABI version. No plugin has permission to reinterpret a new schema as an old one silently.
