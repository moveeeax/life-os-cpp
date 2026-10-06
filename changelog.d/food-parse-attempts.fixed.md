A `food_parse` job that the queue would dead-letter (the provider kept
timing out or answering 429/5xx) no longer stays `queued`: the last attempt
marks it `failed` with `provider_unavailable`, and the page says so instead
of waiting out its deadline. Migration 023 adds `attempts` to
`food_parse_jobs`.
