`GET /api/v1/idempotency/seen?keys=k1,k2` answers which of the caller's
Idempotency-Keys the server already applied (up to 100 per call, kept
`idempotency.ttl_sec`), so an offline queue stops counting a write in flight
twice; `docs/RUNBOOK.md` gains the check that the middleware is on in prod.
