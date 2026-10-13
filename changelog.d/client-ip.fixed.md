The frontend nginx takes the client address from Traefik's X-Forwarded-For,
so the login throttle and the failed-login audit see the client, not the
Traefik pod; until now every login shared one bucket per Traefik pod.
