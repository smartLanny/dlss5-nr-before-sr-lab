# Desktop Entitlement Contract v1

Authentication belongs to the desktop Manager. The game-process rendering add-on must not perform login, HTTP, TLS, QQ membership checks, or remote API calls.

## Intended flow

1. User signs in / redeems access in the desktop Manager.
2. Server returns a short signed entitlement document.
3. Manager stores the signed document locally.
4. The game-side component may verify the signature offline with an embedded public key.
5. Private signing keys and server API secrets never ship in Electron, ReShade add-ons, or game folders.

## Suggested claims

- `version: 1`
- `subject`: opaque account/user id
- `edition`: e.g. `public`, `beta`, `supporter`
- `features`: string array, e.g. `nr_before_sr`, `manager_beta`
- `issuedAt`
- `expiresAt`
- `nonce`
- optional privacy-preserving device binding hash

The document should be signed by the service. Client-side code must only contain the verification public key.

## Product rules

- QQ group number is a public community/support link, never a security credential.
- Losing Internet access should not break an already-valid offline entitlement immediately.
- Authentication failure must not crash or destabilize the game process.
- The rendering add-on should fail to an explicit supported state, not silently alter unrelated DLSS behavior.
- Public/basic features should remain separable from gated beta features so the entire Manager does not become unusable if the auth service is unavailable.

## Prototype status

No real account service, keys, signing logic, telemetry, or network requests are implemented in this branch. This file only fixes the architecture boundary before product integration begins.
