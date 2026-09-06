# Cloud agent notes — #38 follow-up mode1 (沉没之城2 / beta0.4.5)
Mode1 SR→NR. Owner: immediately “未满足NR安全插入条件”.
Smoking gun: `[post-SR] stopped ... reason=null-guard-graphics-state-used prepared-only=1; original Output retained, no Feature18 call`.
Do not treat end.bypass=none/active alone — scope mode1 nrSuccess may be polluted by prior mode2 session counters; judge post-SR stop line + null-guard.
