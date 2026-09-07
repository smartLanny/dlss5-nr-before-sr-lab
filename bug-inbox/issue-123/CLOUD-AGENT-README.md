# Cloud agent notes — issue #123
beta0.4.4-test1 / Starfield.exe / RTX 5070 Ti Laptop.
TAIL: bypass=command-state-unverified; commandState.reason=null-guard-graphics-state-used; dynamic=0x46; nrSuccess=0; route=native-bypass.
Hooks ready + Reset baseline, but null-guard rejects due to graphics state (dynamic 0x46). Early installing alone is NOT the diagnosis.
Related family: command-state / null-pso (#38/#64/#100/#101/#117 path); keep title-specific.
