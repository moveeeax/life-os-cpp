Frontend: one amount parser (`src/lib/amounts.ts`) replaces the ten copies
in money, goals, food and workout forms, so `1,234` is refused instead of
being read as 1.234 and `0x10` or `1e3` are not numbers; the Body page names
a custom period by its dates instead of "Last custom days"; every POST, PUT,
PATCH and DELETE carries an `Idempotency-Key`, minted once per call and
reused on the replay after a token refresh, so a retried batch or parse is
applied once.
