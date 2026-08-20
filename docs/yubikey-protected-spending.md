# YubiKey protected spending (experimental)

This feature adds an optional protected-spending mode to the existing Windows
desktop wallet. It is not a hardware wallet: a standard YubiKey cannot produce
the wallet's ML-DSA signatures and does not display or validate transaction
details. The desktop still constructs and signs each operation.

## Security model

Enabling protection converts the active wallet to a tracking-only wallet. Its
spend seed is removed from both the saved wallet and the resident scanner. The
password-encrypted `.wallet` file embeds one independently YubiKey-encrypted
copy of the seed for every enrolled security key. Each AES-256-GCM encryption
key is derived from that credential's WebAuthn PRF result using HKDF-SHA-256.
The embedded metadata is bound to the wallet's encoded PQ tracking keys. It
contains no PIN, PRF secret, or plaintext seed.

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
3. Read the recovery warning and continue only after confirming that the
   mnemonic is available.
4. Give the first physical key a unique, recognizable label.
5. Complete the Windows Security PIN and touch prompts.
6. Wait until the wallet reports that both protected files were written and
   reopened successfully.
7. Select **Wallet > Add backup YubiKey...** and enroll a different physical
   security key before relying on protected mode.

Activation deliberately creates no full `*.pre-yubikey*.wallet` recovery file.
The wallet first stages and reopens both the active protected tracking wallet
and its protected automatic backup. It verifies their format, tracking
identity, and exact embedded YubiKey metadata before atomically replacing the
active wallet. If staging or validation fails, the original on-disk wallet is
left in place and the detached seed is restored to the in-memory wallet.

Before committing the protected wallet, the application directly removes
known full-wallet bypass files beside it: matching old `pre-yubikey` files and
unprotected or unreadable `.backup` and `.temp` files. Direct deletion does not
use the Windows Recycle Bin. This is not a guaranteed forensic erase on SSDs,
and the application cannot find or remove copies elsewhere, cloud version
history, filesystem snapshots, or external backups. The mnemonic confirmation
is therefore mandatory before the first YubiKey credential is registered.

## Adding backup security keys

An already enrolled key must authorize every addition. The wallet names the
specific enrolled key selected for authorization; after it succeeds, remove
that named key and insert the differently named physical YubiKey that will
become the backup. Windows Security creates a new credential and then verifies
its PRF output before the wallet file is updated. Those are two
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

Prototype wallets that still have a matching `.wallet.yubikey.json` sidecar are
migrated automatically. The sidecar is retained until the new v3 `.wallet` save
has completed and replaced the old file; only then is the matching sidecar
deleted. If the sidecar is corrupt, belongs to another wallet, changes during
migration, or the save fails, it is retained and the old two-file recovery path
continues to work.

If an interrupted old activation left a sidecar next to a full wallet that
still contains its spend seed, the wallet does not claim protection. Preserve
the mnemonic and resolve the orphan sidecar before retrying.

## Backup and recovery

A protected backup consists of one self-contained tracking-only `.wallet` file.
The embedded metadata survives reset/rescan and cache-free manual or automatic
backups. Copying or renaming that `.wallet` therefore cannot separate it from
the YubiKey recovery envelopes. After successful activation, the automatic
`.backup` file is protected in the same way as the active wallet.

When a protected wallet created by an earlier prototype is opened, the wallet
checks for known local full-wallet bypass files and offers to delete them after
the user confirms possession of the mnemonic. Keeping any such file means the
YubiKey protection remains bypassable. This check still cannot discover copies
outside the active wallet directory.

The wallet supports up to eight independently usable security keys. Losing one
does not block spending while another enrolled key remains available. A lost
key's entry cannot currently be removed through the GUI, so label keys clearly
and retain the mnemonic as the final recovery path. Recovery deliberately
creates a normal full wallet; protection can then be enabled again with new
keys.

Protected wallets use wallet serialization version 3. It includes a
compatibility guard because the old version 2 reader did not reject unknown
versions: r11 and older therefore fail their key-integrity check before opening
a v3 wallet and cannot silently strip its embedded metadata on save. Ordinary
full and tracking wallets without protected-spend metadata remain version 2.

Legacy sidecar versions 1 and 2 are accepted only for automatic migration.

## Tests

The automated tests cover independent primary and backup encryption round
trips, embedded metadata serialization, wrong-PRF and tampering rejection,
legacy sidecar compatibility, duplicate-entry rejection, seed-to-tracking-key
validation, the v3 old-reader guard, and wallet detach/save/reload/reset cycles
that prove both that the scanner does not retain the seed and that protected
recovery metadata is never treated as disposable cache.

On Windows, build and run `WebAuthnPrfSmoke` separately. It creates an isolated
test credential, requests two PIN/touch authorizations, and succeeds only when
both operations return the same PRF result for the same credential and salt. It
does not open a wallet or print secret material.

This code is experimental. Do not use it with material funds until the complete
patch has received independent security review and a recovery drill has been
performed on the exact release binary.
