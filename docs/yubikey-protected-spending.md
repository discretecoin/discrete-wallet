# YubiKey protected spending (experimental)

This feature adds an optional protected-spending mode to the existing Windows
desktop wallet. It is not a hardware wallet: a standard YubiKey cannot produce
the wallet's ML-DSA signatures and does not display or validate transaction
details. The desktop still constructs and signs each operation.

## Security model

Enabling protection converts the active wallet to a tracking-only wallet. Its
spend seed is removed from both the saved wallet and the resident scanner. A
companion `<wallet>.yubikey.json` file contains the seed encrypted with
AES-256-GCM. The encryption key is derived from a WebAuthn PRF result using
HKDF-SHA-256. The sidecar is bound to the wallet's encoded PQ tracking keys.

Windows asks the enrolled cross-platform FIDO2 authenticator for user
verification and touch before each operation that needs the seed. The seed is
decrypted only for that operation and is scrubbed afterwards. The first version
covers:

- sending and preparing transactions;
- exporting the mnemonic or hexadecimal spend seed;
- signing messages.

Mining and account registration are disabled in protected mode. Both workflows
need a separate design that preserves the one-operation secret lifetime.

This design improves protection against theft of wallet files and against a
routine process-memory snapshot while the wallet is only synchronizing. It does
not protect against a fully compromised Windows host at the exact time the user
authorizes an operation. Host malware can alter transaction data or capture a
temporarily decrypted seed. There is no trusted transaction display on the
YubiKey.

## Requirements

- Windows with WebAuthn API version 6 or newer;
- a FIDO2 security key that implements the PRF or `hmac-secret` extension;
- a PIN configured on the security key.

The implementation requests a cross-platform authenticator with user
verification required. It does not fall back to a platform authenticator or a
touch-only credential.

## Enabling protection

1. Open a normal full wallet and verify its mnemonic recovery.
2. Select **Wallet > Enable YubiKey protected spending...**.
3. Complete the Windows Security PIN and touch prompts.
4. Wait until the wallet reports that saving has completed.
5. Move the generated `*.pre-yubikey-YYYYMMDD-HHMMSS.wallet` backup offline.

The pre-migration backup is deliberately a complete wallet. Anyone who obtains
that file and its wallet password can spend without the YubiKey.

If an interrupted migration leaves a `.yubikey.json` sidecar next to a wallet
that still contains its spend seed, the next launch displays a critical warning
and does not claim protection. Preserve the pre-migration backup and resolve the
orphan sidecar before retrying; never infer protection merely from the presence
of the JSON file.

## Backup and recovery

A protected backup consists of two files and neither is sufficient alone:

- the tracking-only `.wallet` file;
- the matching `.wallet.yubikey.json` sidecar.

The first version enrolls one security key and has no duplicate-key or disable
workflow. Losing that key makes the protected pair unusable for spending. The
mnemonic or the offline pre-migration full-wallet backup remains the recovery
path. Recovery deliberately creates a normal full wallet; protection can then
be enabled again with a new key.

## Tests

The automated tests cover authenticated-encryption round trips, wrong-PRF and
sidecar-tampering rejection, seed-to-tracking-key validation, and a wallet
detach/save/reload cycle that proves the tracking scanner does not retain the
spend seed.

On Windows, build and run `WebAuthnPrfSmoke` separately. It creates an isolated
test credential, requests two PIN/touch authorizations, and succeeds only when
both operations return the same PRF result for the same credential and salt. It
does not open a wallet or print secret material.

This code is experimental. Do not use it with material funds until the complete
patch has received independent security review and a recovery drill has been
performed on the exact release binary.
