The frontend chart renders its PodDisruptionBudget only when `replicaCount` is above 1, like the API and worker charts, so a single-replica release with `pdb.enabled` no longer blocks node drains.
