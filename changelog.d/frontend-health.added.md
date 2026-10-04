Health section of the dashboard (`/health`): tiles (steps, sleep, resting
heart rate, weight), per-day charts of activity, sleep stages and score, heart
rate, stress and body composition, tables of SpO2 and workouts, all over the
existing `/api/v1/fitness/*` routes for a period of 7, 30 or 90 days or a
custom range kept in the URL. Users with `fitness:sync` also get the coverage
table, a form that starts a sync run and follows it to completion, and a cloud
connection check. Charts use ApexCharts.
