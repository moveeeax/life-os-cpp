Goals: a new module (`GOALS_ENABLED`) for goals of four kinds. A `number`
goal has a start, a target and a unit, and its check-ins (one per date); a
`steps` goal has sections of tasks; a `count` goal counts done tasks against a
target; a `binary` goal has milestones and a result that closes it. Every
goal comes with its progress and pace at the caller's date (`?date=`):
expected value, gap, needed pace per week, "not updated" after 14 days.
Tasks can belong to a goal and to one of its sections, and gain the
`in_progress` status; a goal's undated tasks stay off the agenda's someday.
Migration 030.
