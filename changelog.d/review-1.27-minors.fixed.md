The three minors left from the 1.27 review. Cancelling a job also removes it
from the delayed set, so a retry waiting out its backoff or a deferred job is
not promoted and run after the cancel. A parse or advisor row failed before
its claim (module off, queue gave up) closes only a queued row or a stale
running one; a running row a live worker holds keeps its own outcome.
`config/config.sample.json` lists `/api/v1/account/join-from-invite/*` in
`api.public_paths` like `config.json` and the built-in default do.
