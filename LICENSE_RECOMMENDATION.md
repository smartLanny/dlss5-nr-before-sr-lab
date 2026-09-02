# License recommendation

The staging repository intentionally does not contain a project-wide `LICENSE`.
A private repository without a license preserves the owner's rights while source
ownership and contributor consent are confirmed.

## Recommended choice

After every original contributor confirms that their work may be relicensed,
adopt the MIT License for the project's original source. It is short, familiar to
graphics/modding contributors, compatible with the RenoDX-derived MIT portions,
and compatible with the BSD-style MinHook dependency.

Before adding the license:

1. Identify the copyright holder name to place in the notice.
2. Confirm that every non-trivial copied or adapted portion is listed in
   `THIRD_PARTY_NOTICES.txt` with its required notice.
3. Confirm that no NVIDIA SDK source/header, proprietary binary, model payload,
   or disassembly-derived code is present.
4. Preserve the MinHook license in `third_party/minhook/LICENSE.txt`.
5. Decide whether inbound contributions require a Developer Certificate of
   Origin or an explicit contributor-license statement.

An open-source license would cover only this repository's original source. It
would not grant rights to NVIDIA software, games, ReShade binaries, trademarks,
or separately obtained runtime components.
