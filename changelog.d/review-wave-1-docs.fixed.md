The `runbook_url` of every alert points at this repository on GitHub instead
of a GitLab host; `docs/CONFIG.md` lists the nine job types a worker
registers and the compose stack subscribes to all of them (`email.send` and
`webhook.deliver` were missing); the README quick start says that `make up`
runs the last release image and names the targets that build the checkout.
