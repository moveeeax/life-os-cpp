Controllers share one copy of their helpers: `parse_object`, `no_content` and
the 50/200 list limits in `api/HandlerSupport.hpp`, `require_user` and the
`API_MODULE_USER_GUARD` / `API_MODULE_PERMISSION_USER_GUARD` module guards in
`api/Guards.hpp` (same status codes and bodies), and `SQL_ISO_UTC` in
`repositories/SqlTime.hpp` for the timestamp SQL. The tasks inbox repository
is `InboxRepository`; `Jobs::cancel` is atomic under concurrent calls;
`scripts/new-module.sh` and `scripts/new-react-page.sh` scaffold today's
module switch and kit page.
