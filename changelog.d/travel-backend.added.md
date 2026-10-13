Travel under `/api/v1/travel` (module switch `TRAVEL_ENABLED`): border
crossings, day counters per rule (rolling window, 12 months ending this year,
since the last entry; at most or over a threshold) with the Schengen 90/180
and Kazakhstan tax residency templates, the date a running stay meets the
threshold and the date a full stay fits again; trips with a plan of flights,
stays and notes, packing from the tasks linked by `trip_id`, and expenses
linked to a trip with a total in one currency. `/api/v1/today` carries the
next trip and the counters within 14 days of their threshold.
