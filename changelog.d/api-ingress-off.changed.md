Prod: the API release no longer has an ingress. The frontend release is the
only public entry point of `life-os.tarassov.me` and proxies `/api/` to the API
Service; `/`, `/ready`, `/health` and `/metrics` of the API are reachable only
inside the cluster.
