`GET /api/v1/modules` answers every module switch at once. The app's navigation reads it instead of nine data GETs per load, and asks a module's own route (404 means off) only when that call fails.
