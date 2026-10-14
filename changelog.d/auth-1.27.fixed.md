Security fixes from the review: logout works after the access cookie expired
and revokes the refresh token; `/refresh` refuses an expired token; emails
over 254 bytes fail validation before the regex; API keys lose the admin bit
and answer 401 `key_not_allowed` on `/api/v1/account/*` and `/api/v1/admin/*`
(path canonicalized first); the default role cannot hold the admin bit and the
last admin role cannot lose it; `POST /api/v1/jobs` takes only `echo`, `slow`,
`fail` and `money_rates`; webhook hosts are resolved and refused when any
address is internal; a used reset link invalidates the others; password
reset/change revoke API keys; admin delete or role change drops refresh
sessions; the shared HTTP client follows only http(s) and caps bodies at 16 MiB.
