# Cloud agent notes — issue #121
Assassin's Creed Shadows 1.16 / RTX 5060 / beta0.4.5.
Symptom: flicker; Home overlay feels like rapid enable toggling.
Regression: OK on ~0.3.3.4; broken 0.3.3.5 through 0.4.5.
Judge by rapid none/active ↔ command-state-unverified thrashing (hundreds of transitions). Early installing/native-hooks-unavailable alone is NOT terminal.
