`POST /api/v1/fitness/sync` refuses a range wider than 366 days with 400 `range_too_wide`, like the export: one run holds the account's sync slot until it ends.
