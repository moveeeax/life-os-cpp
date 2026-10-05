# Multi-stage build for the React SPA.
# Build context is the REPO ROOT so a future codegen step could reach
# docs/openapi.yaml; today every COPY is prefixed with `frontend/`.
# Final image is ~125MB: alpine and the JS bundle (~25MB) plus ~100MB of
# exercise photos for the Workout section.

# ── builder ─────────────────────────────────────────────────────────────
FROM node:22-alpine AS builder

WORKDIR /app

# Manifest first for layer caching: package.json + lock change → reinstall;
# source change alone keeps node_modules cached.
COPY frontend/package.json frontend/package-lock.json* ./
RUN if [ -f package-lock.json ]; then npm ci --no-audit --no-fund; \
    else npm install --no-audit --no-fund; fi

# Build configuration files.
COPY frontend/tsconfig.json frontend/tsconfig.node.json frontend/vite.config.ts ./
COPY frontend/eslint.config.js frontend/.prettierrc.json frontend/index.html ./

# Source.
COPY frontend/src ./src
COPY frontend/public ./public

RUN npm run build

# ── exercise photos ─────────────────────────────────────────────────────
# Photos of the workout exercise library (yuhonas/free-exercise-db, Unlicense),
# two per exercise. The commit is the one migrations/018 was generated from
# (scripts/gen-exercise-seed.py): the `images` paths in the database are
# relative to this directory. The archive is checked against a pinned SHA-256;
# GitHub builds commit archives on demand, so if the check ever fails with the
# commit unchanged, compare the unpacked files before updating the sum.
FROM node:22-alpine AS exercise-media

ARG EXERCISE_DB_COMMIT=f00c92c7dcf1216a928a52c3706c7ce8e2f71ed5
ARG EXERCISE_DB_SHA256=653114b84e2b37a97fa211263fc98dcb076109ea1a6bb896c64d5015e74ebd99

RUN wget -q -O /tmp/db.tar.gz \
        "https://codeload.github.com/yuhonas/free-exercise-db/tar.gz/${EXERCISE_DB_COMMIT}" \
    && echo "${EXERCISE_DB_SHA256}  /tmp/db.tar.gz" | sha256sum -c - \
    && mkdir -p /tmp/db /exercise-media \
    && tar -xzf /tmp/db.tar.gz -C /tmp/db --strip-components=1 \
    && find /tmp/db/exercises -mindepth 1 -maxdepth 1 -type d -exec mv {} /exercise-media/ \; \
    && rm -rf /tmp/db /tmp/db.tar.gz \
    && chmod -R a+rX /exercise-media

# ── runtime ─────────────────────────────────────────────────────────────
# Unprivileged base: master + workers run as the non-root "nginx" user (uid
# 101) and the server listens on 8080, so the image runs under a restrictive
# podSecurityContext (runAsNonRoot) without a privileged port or root pidfile.
FROM nginxinc/nginx-unprivileged:1.27-alpine AS runtime

# Photos first: this layer changes only with the pinned commit.
COPY --from=exercise-media /exercise-media /usr/share/nginx/html/exercise-media
COPY --from=builder /app/dist /usr/share/nginx/html
COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
    CMD wget -q -O - http://localhost:8080/healthz || exit 1
