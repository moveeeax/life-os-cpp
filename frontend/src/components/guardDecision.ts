import type { User } from '@/lib/api/types';

/** Minimal shape of the useMe() query result the guard decision depends on. */
export interface GuardSessionState {
  isPending: boolean;
  isError: boolean;
  /**
   * Resolved user, or null on a 401 (logged out). Narrowed to just the
   * fields the guard reads (`confirmed` for the unconfirmed redirect, `role`
   * for the permission check via userCan) so no `as never` cast is needed.
   */
  data: Pick<User, 'confirmed' | 'role'> | null | undefined;
}

export type GuardDecision =
  | { kind: 'loading' }
  | { kind: 'error' }
  | { kind: 'redirect'; to: '/login' | '/unconfirmed' | '/' }
  | { kind: 'allow' };

/**
 * Pure routing decision for ProtectedRoute, extracted so it can be unit
 * tested without rendering a React tree. It lives next to ProtectedRoute in its
 * own module so the component file exports only components (fast refresh).
 *
 * The critical invariant the round-2 audit cares about:
 *   - isError with no cached user → 'error' (show retry), NEVER a /login redirect
 *   - isError with a cached user → decide on the cached user
 *   - data===null (401) → redirect to /login
 */
export function guardDecision(
  me: GuardSessionState,
  opts: { requirePermission?: number; requireConfirmed?: boolean },
  hasPermission: (user: NonNullable<GuardSessionState['data']>, bit: number) => boolean,
): GuardDecision {
  if (me.isPending) return { kind: 'loading' };
  // A cached user wins over a failed background refetch: a blip while the
  // tab revalidates must not replace the page with the error state.
  const user = me.data ?? null;
  // A thrown error is a *real* failure (network / 5xx); 401 resolves to
  // null instead. Never bounce a possibly-logged-in user to /login here.
  if (!user && me.isError) return { kind: 'error' };
  if (!user) return { kind: 'redirect', to: '/login' };
  if (opts.requireConfirmed && !user.confirmed) return { kind: 'redirect', to: '/unconfirmed' };
  if (opts.requirePermission !== undefined && !hasPermission(user, opts.requirePermission)) {
    return { kind: 'redirect', to: '/' };
  }
  return { kind: 'allow' };
}
