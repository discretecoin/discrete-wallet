# Discrete Services desktop sign-in

The account page opens `discrete:login?intent=<64 hexadecimal characters>`.
The wallet displays its fixed website origin and asks for explicit approval.
After approval it signs locally and opens the account page in the system's
default browser. No payment, transaction, seed export or wallet RPC is involved.

Start sign-in in the normal profile of that same default browser. An embedded
browser or another profile can have a different cookie store and cannot claim
the handoff. An existing session in a different browser is not evidence that
the new attempt succeeded.

## Request and return contract

The production origin is `https://services.discrete.cash`. Builds may override
it only with explicit IPv4 loopback HTTP for isolated qualification. Incoming
links cannot select an origin, callback, message or challenge. Only one intent
parameter is accepted; additional parameters, duplicates and fragments fail.
Both `discrete:login?...` and `discrete://login?...` forms are recognized.

1. A same-origin site request issues a short-lived HttpOnly/SameSite=Strict
   browser-intent cookie. The URI carries its SHA-256 hash, not the cookie secret.
2. After the user confirms, the wallet independently generates a 32-byte nonce
   and a 32-byte handoff secret using the system random source. Both are encoded
   as lowercase hexadecimal. The handoff hash is SHA-256 of that ASCII encoding.
3. `POST /api/auth/wallet/start` receives JSON containing `address`, `walletNonce`,
   `intentHash` and `handoffHash`. The address is the opened mainnet wallet's full
   base PQ address. Native requests use `X-Discrete-Wallet: 1`, without browser
   Origin/Fetch Metadata headers.
4. The response supplies `id`, `identity`, `network`, `expires` and `message`.
   The wallet reconstructs the exact message from the fixed origin, mainnet,
   returned identity/request/expiry, original intent and its own nonce/handoff
   hash. It requires a canonical non-null UUID, 64-character hexadecimal identity,
   expiry within three minutes, and byte-for-byte matching message. The service's
   grant lifetime is two minutes. Signing uses the existing message-signing API.
5. `POST /api/auth/wallet/prove` receives `id`, `handoff` and `signature`.
   Only `{ "approved": true }` permits the fixed browser return:
   `/account/#wallet-login=<id>.<handoff>`.
6. The receiving site removes the fragment immediately and claims the grant
   with a same-origin POST. It must verify the signature and require both the
   wallet-created secret and the exact browser intent cookie. A successful claim
   consumes both grant and intent and issues the normal browser session.

No authentication request or signature occurs before approval. Cancel aborts
in-flight requests. HTTP redirects, unexpected responses, response bodies over
32,000 bytes and network failures fail closed. Each request has a ten-second
transfer timeout. Retry creates a fresh nonce and handoff. Failure to launch the
browser can retry the approved fixed destination without another signature.

## Security boundaries

A forwarded intent does not reveal the wallet-created handoff secret. The wallet
returns that secret through its local browser, not to an attacker-selected
callback or initiating remote browser. A grant for another intent is rejected
even when the receiving browser has some other valid login intent. This also
prevents substituting an attacker's approved account into the victim's browser.

The matching service must enforce exact Origin/Host, HTTPS, short expiry,
single-use transactional claim, bounded pending state, rate limits and no logging
of signatures, handoff URLs or request bodies. Keep secrets out of query strings.
It must disable legacy manual website session endpoints that would otherwise
preserve a relay bypass. The generic GUI Sign message dialog refuses the reserved
`Wallet-directed browser login v1` protocol marker; ordinary signing is retained.

This is not remote attestation. Another wallet or CLI may still sign arbitrary
login text. Deliberately signing an attacker-supplied login message with another
signer, sharing an unused handoff, a compromised website/browser/OS/wallet or
stolen session cookies are outside this flow's relay protection. The feature
does not claim WebAuthn-equivalent protection across arbitrary signers.

## Qualification and operation

`WebsiteLoginTests` uses the production dialog with a local mock server and
synthetic signer. It covers consent, URI restrictions, fresh secrets, modified
message/intent, fixed destination, redirects, size limits, offline/retry, wallet
changes and cancellation before/during requests. Run it with the exact build's
Qt DLL/plugin paths; headless runs set `QT_QPA_PLATFORM=offscreen`.

`WebsiteLoginIntegration` is an opt-in target. It creates an unfunded process-local
Core identity and uses actual Core ML-DSA signing through the same dialog. It is
fixed to loopback port 38889, independently of the production wallet origin.
Arguments are an output filename and the matching website's browser-intent hash.
The test intercepts the browser opener and writes a one-time synthetic handoff
for an external browser harness. Never publish that file or use owner keys in
the harness. This test does not register an OS handler or open an owner wallet.

Windows startup registers the URI handler for the current user, allowing a normal
portable launch to select its executable without administrator rights. Do not
run competing wallet versions against the same wallet file. Official installers,
closed-wallet launch, other supported OS/browser profiles and the final HTTPS
service need release qualification. A loopback preview is not a production build.
