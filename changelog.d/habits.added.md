Habits under `/api/v1/habits` (module switch `HABITS_ENABLED`, migration 037):
habits every day or N times a week with an optional goal, ticked by hand or
from data (sleep, steps and active kcal from Mi Fitness, a finished Training
session), evaluated on read on the caller's local day, with the current and
the best streak. A Habits section (Today and Week) and a Habits plate on Now
that Arrange can place; `/api/v1/today` gains the `habits` part.
