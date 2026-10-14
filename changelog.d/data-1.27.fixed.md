Data and job fixes from the 2026-10-14 review. Deleting a user now also
deletes the Mi Fitness rows of the Xiaomi account they linked, and only the
billing foreign keys are reported as `user_has_billing_history`. A parse or
advisor job whose row another delivery still holds waits until the row turns
stale instead of spending its retries; every write after the claim is fenced
by the attempt; a row whose worker died reads as `failed` (`worker_lost`), and
a row the queue gave up on is closed as `failed` (`queue_gave_up`). Queued
rows older than ten minutes no longer count toward `too_many_parses`. Retry
backoff is on by default (`JOBS_RETRY_BACKOFF_BASE_MS=2000`). A late Xiaomi
login can no longer overwrite a fresh re-link (writes fenced by `rotated_at`).
With `?tz=` and no date, Today, Tasks, Goals, Travel and the Money report and
parse take today in that zone instead of UTC. Habit streaks no longer lose
their newest days when an old range is viewed. A money account or category
delete that loses a race answers 409 `in_use`, not 500. Money amounts with
more than 15 significant digits are refused (`precision`). `/today` counts the
inbox instead of loading it, and three foreign keys got indexes (migration
040).
