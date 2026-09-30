-- Migration 015: fitness_roles — seed the two fitness roles.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Permission bits (src/domain/Role.hpp):
--   0x01 GENERAL, 0x04 FITNESS_READ, 0x08 FITNESS_SYNC.
-- Fitness Reader   = 0x05: read /api/v1/fitness/* — Life OS agents via API key.
-- Fitness Operator = 0x0D: reader + probe + sync enqueue/status.
-- Administrator passes every fitness guard through the kAdminister sentinel.

INSERT INTO roles (name, permissions, is_default) VALUES
    ('Fitness Reader',   5,  FALSE),
    ('Fitness Operator', 13, FALSE)
ON CONFLICT (name) DO NOTHING;
