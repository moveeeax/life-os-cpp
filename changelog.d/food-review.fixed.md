Food module after review. A `food_parse` row can no longer stay `running`:
an unexpected error after the claim requeues the job or fails it on the
last attempt, a row a crashed worker left behind is claimed again after
10 minutes (migration 024, `started_at`), the retry limit travels with the
job, and a `null` model in the answer is tolerated. Dates are checked as
calendar days between 1900 and 2100 (`2026-02-30` is a 400, not a 503);
`null` in a required entry field is a 400, not a 500; names and notes are
limited and cut by characters, so a Cyrillic name has the same room as an
English one; a copy from Open Food Facts obeys the API's own limits; two
parallel copies of one barcode no longer answer 500; `%` and `_` in the
product search are text; a moved entry goes to the end of its new meal;
the parse prompt gets the products touched last; at most three parses per
user may be open at once (429 `too_many_parses`).
