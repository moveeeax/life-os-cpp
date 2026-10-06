/**
 * Centralised TanStack Query key factory.
 *
 * One place owns the cache-key shapes so invalidation stays consistent:
 * a mutation invalidates `qk.admin.users()` and every paged variant
 * (usePagedQuery appends the page number to the end) is matched by the
 * prefix. Keep keys as `as const` tuples so TypeScript narrows them.
 */
export const qk = {
  me: () => ['me'] as const,
  billing: {
    packages: () => ['billing', 'packages'] as const,
    /** Own wallet balance + ledger page. usePagedQuery-style: page appended by callers. */
    wallet: (page?: number) =>
      page === undefined
        ? (['billing', 'wallet'] as const)
        : (['billing', 'wallet', page] as const),
  },
  /** Health section: every key starts with 'health' so a finished sync can invalidate them all. */
  health: {
    all: () => ['health'] as const,
    list: (route: string, from: string, to: string) => ['health', route, from, to] as const,
    coverage: () => ['health', 'coverage'] as const,
    syncRun: (id: number) => ['health-sync-run', id] as const,
  },
  /** The caller's Mi account link. */
  mi: {
    account: () => ['mi', 'account'] as const,
    link: (id: string) => ['mi', 'link', id] as const,
  },
  /** Workout section. Lists are keyed under a prefix so one write invalidates every variant. */
  workout: {
    exercises: (filter?: Record<string, string>) =>
      filter === undefined
        ? (['workout', 'exercises'] as const)
        : (['workout', 'exercises', JSON.stringify(filter)] as const),
    routines: () => ['workout', 'routines'] as const,
    routine: (id: string) => ['workout', 'routine', id] as const,
    exercise: (id: string) => ['workout', 'exercise', id] as const,
    active: () => ['workout', 'active'] as const,
    sessions: () => ['workout', 'sessions'] as const,
    session: (id: string) => ['workout', 'session', id] as const,
    heartRate: (id: string) => ['workout', 'heart-rate', id] as const,
    readiness: () => ['workout', 'readiness'] as const,
    /** Whether the module answers at all (it is 404 while switched off). */
    enabled: () => ['workout-enabled'] as const,
  },
  /** Food section. Diary keys share the 'food' prefix so an entry write invalidates day and week. */
  food: {
    day: (date: string) => ['food', 'day', date] as const,
    week: (from: string) => ['food', 'week', from] as const,
    diary: () => ['food', 'day'] as const,
    weeks: () => ['food', 'week'] as const,
    goals: () => ['food', 'goals'] as const,
    items: (q?: string, archived?: boolean) =>
      q === undefined
        ? (['food', 'items'] as const)
        : (['food', 'items', q, archived ? 'all' : 'active'] as const),
    recent: () => ['food', 'recent'] as const,
    offSearch: (q: string) => ['food', 'off', q] as const,
    parse: (id: string) => ['food', 'parse', id] as const,
    /** Whether the module answers at all (it is 404 while switched off). */
    enabled: () => ['food-enabled'] as const,
  },
  admin: {
    users: (page?: number) =>
      page === undefined ? (['admin', 'users'] as const) : (['admin', 'users', page] as const),
    user: (id: string) => ['admin', 'user', id] as const,
    roles: () => ['admin', 'roles'] as const,
    jobs: (filter?: string, page?: number) => {
      if (filter === undefined) return ['admin', 'jobs'] as const;
      if (page === undefined) return ['admin', 'jobs', filter] as const;
      return ['admin', 'jobs', filter, page] as const;
    },
    jobsDlq: () => ['admin', 'jobs-dlq'] as const,
    /**
     * Audit trail list. `filters` is the active filter object; serialising
     * it into the key means a changed filter is a fresh cache entry, while
     * the bare prefix (['admin','audit']) still matches every variant for
     * invalidation. usePagedQuery appends the page number on top.
     */
    audit: (filters?: Record<string, string>) =>
      filters === undefined
        ? (['admin', 'audit'] as const)
        : (['admin', 'audit', JSON.stringify(filters)] as const),
    billing: {
      packages: () => ['admin', 'billing', 'packages'] as const,
      /**
       * Payments list. `filter` is the serialised status filter — a changed
       * filter is a fresh cache entry; the bare prefix still matches every
       * variant for invalidation. usePagedQuery appends the page number.
       */
      payments: (filter?: string, page?: number) => {
        if (filter === undefined) return ['admin', 'billing', 'payments'] as const;
        if (page === undefined) return ['admin', 'billing', 'payments', filter] as const;
        return ['admin', 'billing', 'payments', filter, page] as const;
      },
      settings: () => ['admin', 'billing', 'settings'] as const,
      /**
       * Metrics snapshot for a future dashboard (see the extension-point
       * note in pages/admin/Billing.tsx). Keyed by period so switching a
       * day/week/month toggle is a fresh cache entry (and a refetch).
       */
      metrics: (period: 'day' | 'week' | 'month') =>
        ['admin', 'billing', 'metrics', period] as const,
    },
  },
} as const;
