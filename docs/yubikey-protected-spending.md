# YubiKey protected spending (experimental)

This feature adds an optional protected-spending mode to the existing Windows
desktop wallet. It is not a hardware wallet: a standard YubiKey cannot produce
the wallet's ML-DSA signatures and does not display or validate transaction
details. The desktop still constructs and signs each operation.

## Security model

Enabling protection converts the active wallet to a tracking-only wallet. Its
spend seed is removed from both the saved wallet and the resident scanner. A
companion `<wallet>.yubikey.json` file contains one independently encrypted copy
of the seed for every enrolled security key. Each AES-256-GCM encryption key is
derived from that credential's WebAuthn PRF result using HKDF-SHA-256. The
sidecar is bound to the wallet's encoded PQ tracking keys.

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
6. Select **Wallet > Add backup YubiKey...** and enroll a different physical
   security key before relying on protected mode.

The pre-migration backup is deliberately a complete wallet. Anyone who obtains
that file and its wallet password can spend without the YubiKey.

## Adding backup security keys

An already enrolled key must authorize every addition. After that
authorization succeeds, remove it and insert the different physical YubiKey
that will become the backup. Windows Security creates a new credential and then
verifies its PRF output before the sidecar is updated atomically. Those are two
separate PIN/touch prompts on the new key; both are required during enrollment.

The wallet stores a user label and a short hash fingerprint for selection. FIDO2
does not expose a stable per-device serial number here, so the wallet cannot
prove that two credentials reside on different physical keys. Creating another
credential on the same YubiKey does not provide redundancy. Perform a recovery
drill: close the wallet, remove the primary key, and authorize a protected
operation using only the backup.

Each enrolled key can unlock the wallet independently; this is an any-one-key
recovery design, not a multi-signature or threshold scheme. Adding more keys
therefore improves availability but also means that theft of any enrolled key
and its PIN is sufficient to authorize an operation on a compromised host.

If an interrupted migration leaves a `.yubikey.json` sidecar next to a wallet
that still contains its spend seed, the next launch displays a critical warning
and does not claim protection. Preserve the pre-migration backup and resolve the
orphan sidecar before retrying; never infer protection merely from the presence
of the JSON file.

## Backup and recovery

A protected backup consists of two files and neither is sufficient alone:

- the tracking-only `.wallet` file;
- the matching `.wallet.yubikey.json` sidecar.

The sidecar supports up to eight independently usable security keys. Losing one
does not block spending while another enrolled key remains available. A lost
key's entry cannot currently be removed through the GUI, so label keys clearly
and retain the mnemonic or offline pre-migration full-wallet backup as the final
recovery path. Recovery deliberately creates a normal full wallet; protection
can then be enabled again with new keys.

Version 2 of the sidecar stores the multi-key list. The wallet reads existing
single-key version 1 sidecars and upgrades them atomically when a backup key is
added. Older prototype builds cannot read a version 2 sidecar; they fail closed
without modifying it.

## Tests

The automated tests cover independent primary and backup encryption round
trips, wrong-PRF and sidecar-tampering rejection, version 1 compatibility,
version 2 persistence and duplicate-entry rejection, seed-to-tracking-key
validation, and a wallet detach/save/reload cycle that proves the tracking
scanner does not retain the spend seed.

On Windows, build and run `WebAuthnPrfSmoke` separately. It creates an isolated
test credential, requests two PIN/touch authorizations, and succeeds only when
both operations return the same PRF result for the same credential and salt. It
does not open a wallet or print secret material.

This code is experimental. Do not use it with material funds until the complete
patch has received independent security review and a recovery drill has been
performed on the exact release binary.
