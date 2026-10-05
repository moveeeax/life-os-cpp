Mi account linking per user (backend). A signed-in user links a Xiaomi
account by QR sign-in through `/api/v1/fitness/account/*`: start an attempt,
confirm it in a Xiaomi app, and the service verifies the token, finds the
account's cloud region and stores the link sealed. The link has a status:
`ok`, or `reauth_required` once Xiaomi refuses the stored token. Credentials
moved from the single-row `xiaomi_credentials` to `mi_accounts` (one row per
user; migration 020 gives the existing account to the oldest administrator).
