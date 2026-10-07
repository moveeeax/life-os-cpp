Money: `PATCH /api/v1/money/merchants` takes `{merchant_key, category_id}` in
the body and replaces `PATCH /api/v1/money/merchants/{key}`, since keys hold
spaces, `&`, `'` and any script. A display name is normalized to its key.
`POST /api/v1/money/rates/refresh` with a range answers 409
`backfill_running` while earlier rate jobs still wait in the queue and the
range has days without rates, so no day is queued twice.
