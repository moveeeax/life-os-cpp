`fail()`, `defer()` and `dead_letter()` put the job on its next list (live
queue, delayed set or DLQ) before releasing it from the worker's processing
list, so a crash between the two writes duplicates the job instead of losing
it; `pick()`, `recover_processing()` and `promote_due_jobs()` skip a job
whose blob is already completed, dead or cancelled, so a cancel that lost a
race with a pop, or a crash right after a completion, cannot run the job
again.
