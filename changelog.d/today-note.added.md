`GET /api/v1/today` carries the note of the day as `note` (a string, or null
when there is none), so a client reads back what `PUT /api/v1/today/note`
wrote from another device.
