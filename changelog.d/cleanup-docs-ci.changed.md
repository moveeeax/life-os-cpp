The helm gate now renders the real prod values in `deploy/` and requires rate
limiting (fail-closed), CSRF and idempotency there; `release.sh` bumps and
`check-version-sync.sh` checks the prod image tags; the unused stage overlay is
gone; the release workflow scans the frontend image and no longer runs twice
for one tag; CI lints the OpenAPI spec; docs match the current prod setup.
