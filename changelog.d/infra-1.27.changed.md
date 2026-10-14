Infra and CI: the worker pod waits in a `wait-for-schema` init container
until the database carries every migration of its version (API image,
`--verify-migrations`, 10-minute cap), and the upgrade order API, worker,
frontend is in `docs/RUNBOOK.md` and the `release.sh` next steps.
`release.yml` builds only a tag on `master` whose commit passed CI, refuses a
manual rebuild of a published version without `force`, signs before promote
and fails on fixable HIGH as well as CRITICAL. CI scopes a push by the whole
push and no longer cancels runs on `master`; helm and yq downloads are pinned
by sha256. The frontend nginx gzips text assets, hides its version and sends
`form-action 'self'`. `docs/openapi.yaml` and `frontend/package.json` carry
the app version (checked by `check-version-sync.sh`, bumped by `release.sh`);
the `values-prod.example.yaml` overlays are gone; README, ARCHITECTURE and
the RUNBOOK backup and restore section match the cluster.
