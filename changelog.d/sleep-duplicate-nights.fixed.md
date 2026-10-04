Fitness sleep sync stored one night as several rows: the Xiaomi cloud keeps
each partial upload of a night as its own record, and `sleep_id` was derived
from the record time. `sleep_id` is now derived from the bedtime, snapshots
of one night collapse to the one that ends last, and a shorter snapshot no
longer overwrites a longer stored night. Migration 016 removes the existing
duplicates and re-keys `sleep_sessions.sleep_id`.
