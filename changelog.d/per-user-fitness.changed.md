Fitness and Workout data are per user. Every fitness route works on the
caller's own Mi account: reads return only the rows of that account (a user
without a link gets empty lists), a sync run belongs to an account and is
visible only to it, and the schedule enqueues one sync per linked account
whose token Xiaomi still accepts. Workout readiness, the body-weight snapshot
and the match with band data read the session owner's account. The default
role `User` gets `fitness:read` and `fitness:sync` (migration 021), so every
confirmed user has Health and Workout for their own data. An API key reads
the data of its own user: a key issued under another user no longer sees the
owner's data.
