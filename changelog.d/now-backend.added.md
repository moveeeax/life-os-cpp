Now: a new module (`NOW_ENABLED`) behind the Now screen. `GET /api/v1/today`
gives the day's digest in one call: late and today's tasks, active goals with
their pace (behind first), food eaten against the targets, money spent today
per currency with the month's projection and usual spend, last night's sleep,
the day's steps and the latest weight. A section that is off or fails is left
out and named in `errors`. The focus of the day (`/api/v1/today/focus`) is a
task, a goal or typed text; ticking a task focus closes the task. The
wallpaper library (`/api/v1/wallpapers`, `/api/v1/admin/wallpapers`) keeps
videos and images in an S3-compatible bucket (`S3_*` settings): the admin
uploads straight to the bucket through presigned URLs, and every user lists
the ready ones per device. Migration 031.
