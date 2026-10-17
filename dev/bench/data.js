window.BENCHMARK_DATA = {
  "lastUpdate": 1792229809016,
  "repoUrl": "https://github.com/moveeeax/life-os-cpp",
  "entries": {
    "Throughput": [
      {
        "commit": {
          "author": {
            "name": "Michael Tarassov",
            "username": "moveeeax",
            "email": "michael@tarassov.me"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "1bfb79dc15a2f4f4d8313b93d10ef9bd7278bcfa",
          "message": "chore(release): 1.30.0 — Now dock and corners, quick links, migrate init AUTH_MODE (#128)",
          "timestamp": "2026-10-15T08:44:33Z",
          "url": "https://github.com/moveeeax/life-os-cpp/commit/1bfb79dc15a2f4f4d8313b93d10ef9bd7278bcfa"
        },
        "date": 1792229806171,
        "tool": "customBiggerIsBetter",
        "benches": [
          {
            "name": "healthz req/s",
            "value": 38365.38,
            "unit": "req/s"
          },
          {
            "name": "jobs req/s",
            "value": 8161.82,
            "unit": "req/s"
          }
        ]
      }
    ],
    "Latency & footprint": [
      {
        "commit": {
          "author": {
            "name": "Michael Tarassov",
            "username": "moveeeax",
            "email": "michael@tarassov.me"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "1bfb79dc15a2f4f4d8313b93d10ef9bd7278bcfa",
          "message": "chore(release): 1.30.0 — Now dock and corners, quick links, migrate init AUTH_MODE (#128)",
          "timestamp": "2026-10-15T08:44:33Z",
          "url": "https://github.com/moveeeax/life-os-cpp/commit/1bfb79dc15a2f4f4d8313b93d10ef9bd7278bcfa"
        },
        "date": 1792229808611,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "healthz p50",
            "value": 4.8,
            "unit": "ms"
          },
          {
            "name": "healthz p99",
            "value": 17.85,
            "unit": "ms"
          },
          {
            "name": "jobs p50",
            "value": 24.88,
            "unit": "ms"
          },
          {
            "name": "jobs p99",
            "value": 51.65,
            "unit": "ms"
          },
          {
            "name": "runtime image size",
            "value": 112.7,
            "unit": "MB"
          },
          {
            "name": "cold start to /ready",
            "value": 905,
            "unit": "ms"
          },
          {
            "name": "idle RSS",
            "value": 4.2,
            "unit": "MB"
          }
        ]
      }
    ]
  }
}