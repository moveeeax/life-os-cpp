Tasks: `%` and `_` in the search are literal, `done_since` counts from
midnight UTC, the someday list is newest first, an empty due from the phrase
reader means no date, a failed parse says what went wrong in words, and a
task's details have "Move date". A task from a ledger row opens that row
(`/money?row=<id>`). Goals: a check-in before the goal's start is refused, a
pass / fail goal is done exactly when its result is set (no closing without
one, no reopening with one), Passed and Not passed ask before they close the
goal, the list reads every check-in in one query, and a task of a closed goal
still shows that goal in its form. Money: refreshing today's rates while a
rates job waits queues nothing new and answers `already_queued`.
