Invite-only registration: `auth.registration_open` / `REGISTRATION_OPEN`
(default `true`, `false` in prod) makes `POST /api/v1/auth/register` answer 403
`registration_closed` while admin invites keep working, and the public probe
`GET /api/v1/auth/registration` returns `{"data":{"open":bool}}` for the
sign-in page.
