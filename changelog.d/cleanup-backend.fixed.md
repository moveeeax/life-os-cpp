Backend fixes from a review: parse jobs left running by a dead worker no
longer lock a user out of new parses, and a late result cannot overwrite a
finished one; a money advisor review the queue refused is marked failed, and
Run now rewrites a review made before its period ended; the focus of the day
handles Cyrillic titles; a failed fitness probe answers with a JSON 503;
deleting a user removes their money, tasks, goals, food, workout and focus
rows; a refresh token can be used only once even under a race; a racing
workout start is a 409; the exercise search matches `%` and `_` literally;
money amounts go down to one satoshi.
