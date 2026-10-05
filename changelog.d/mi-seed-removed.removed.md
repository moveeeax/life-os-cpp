The Xiaomi credential seed from the environment: `MI_FITNESS_USER_ID`,
`MI_FITNESS_PASS_TOKEN`, `MI_FITNESS_REGION` and `MI_FITNESS_RESEED` are no
longer read. An account is linked from the profile; the region is detected
per account. `GET /api/v1/fitness/probe` without a linked account answers 409
`not_linked` instead of 503 `not_configured`.
