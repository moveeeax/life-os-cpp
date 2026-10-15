import { type ReactNode } from 'react';
import { Navigate, useLocation } from 'react-router';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { useMe } from '@/hooks/useMe';
import { apiErrorMessage } from '@/lib/api/client';
import { userCan } from '@/lib/auth/permissions';

import { guardDecision } from './guardDecision';

interface ProtectedRouteProps {
  children: ReactNode;
  /** Required permission bit (defaults to "any authenticated user"). */
  requirePermission?: number;
  /** When true, redirect logged-in-but-unconfirmed users to /unconfirmed. */
  requireConfirmed?: boolean;
}

/** The location to come back to after sign-in: path, query and hash. */
export type FromState = { from?: string };

/**
 * Wraps a page with auth guards:
 *   - Not logged in → /login (with `from` state for redirect-after-login)
 *   - Missing permission → /
 *   - Unconfirmed (and requireConfirmed) → /unconfirmed
 *
 * Reads the user directly from the useMe() query — the TanStack Query
 * cache is the single source of truth for the session.
 */
export function ProtectedRoute({
  children,
  requirePermission,
  requireConfirmed,
}: ProtectedRouteProps) {
  const location = useLocation();
  const me = useMe();

  const decision = guardDecision(me, { requirePermission, requireConfirmed }, (user, bit) =>
    userCan(user, bit),
  );

  switch (decision.kind) {
    case 'loading':
      // isPending = no data and no error yet. Distinct from isFetching,
      // which can flip true on background revalidations of cached data.
      return <div className="container mx-auto py-8 text-muted-foreground">Loading…</div>;
    case 'error':
      // A thrown error from useMe is a *real* failure (network / 5xx) — the
      // 401 "no session" case resolves to null instead. Don't bounce a
      // possibly-logged-in user to /login on a transient blip; let them retry.
      return (
        <div className="container mx-auto max-w-md py-8 space-y-4">
          <Alert variant="destructive">
            <AlertDescription>
              {apiErrorMessage(me.error, 'Could not load your session.')}
            </AlertDescription>
          </Alert>
          <Button onClick={() => me.refetch()} disabled={me.isFetching}>
            {me.isFetching ? 'Retrying…' : 'Retry'}
          </Button>
        </div>
      );
    case 'redirect':
      return (
        <Navigate
          to={decision.to}
          replace
          // The whole location, so /money?row=<id> or /food?date=… survive the sign-in.
          state={
            decision.to === '/login'
              ? ({ from: location.pathname + location.search + location.hash } satisfies FromState)
              : undefined
          }
        />
      );
    case 'allow':
      return <>{children}</>;
  }
}
