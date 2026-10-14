A delete refused by an `ON DELETE RESTRICT` foreign key answers 409 (`in_use`,
`role_in_use`, billing history) on PostgreSQL 18 too: that major reports the
violation as SQLSTATE 23001 (`restrict_violation`), which the code treated as
an unknown error and turned into a 500. Tests and compose now run Postgres 18,
which is how it showed up.
