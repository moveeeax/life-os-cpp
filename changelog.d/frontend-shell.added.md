Dashboard shell in the frontend, adapted from TailAdmin React: `/` is a
full-screen sign-in form for a guest and a redirect to `/health` for a signed-in
user, `/login` redirects to `/`, and `/health` opens inside a sidebar layout
that lists the Life OS sections (Health is active, the rest are marked "soon").
The Health page is a placeholder until its charts ship. Admin and account pages
keep their layout. `deploy/values-frontend-prod.yaml` holds the prod values of
the frontend chart.
