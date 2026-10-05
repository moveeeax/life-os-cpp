Workout module, second part: logged sessions under
`/api/v1/workout/sessions` (start empty or from a routine, sets with
client-generated ids so a resend replaces itself, history with volume),
`/api/v1/workout/readiness`, and matching of finished sessions with Mi Fitness
heart-rate samples and band workouts. The match runs when a session is
finished or moved, after every fitness sync, and on
`POST /api/v1/workout/reconcile` (`fitness:sync`).
