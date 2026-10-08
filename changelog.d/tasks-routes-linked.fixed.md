Tasks: the `/api/v1/tasks` routes answered 404 in 1.16.0 because the
controller was not compiled into the API binary (it was missing from
`src/api/Api.hpp`, the header that pulls every controller in).
`scripts/check-routes-registered.sh` now fails when a controller with routes
is not included there.
