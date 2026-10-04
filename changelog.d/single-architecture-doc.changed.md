The ADRs in `docs/adr/` are replaced by one `docs/ARCHITECTURE.md`. Template
and flask-base wording is removed from docs, comments and names: `GET /` now
returns `"message": "life-os-cpp"` (was `"C++ API Template"`), and the OpenAPI
title is `life-os-cpp API`. Migrations 000 and 001 changed in comments only;
a database that already applied them needs
`UPDATE schema_migrations SET checksum = NULL WHERE version IN (0, 1)` before
it boots this version with migrations enabled.
