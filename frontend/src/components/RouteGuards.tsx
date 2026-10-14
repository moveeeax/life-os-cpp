import { Suspense } from 'react';
import { Outlet, useRouteError } from 'react-router';

import { ProtectedRoute } from '@/components/ProtectedRoute';
import { Permission } from '@/lib/auth/permissions';

/**
 * Guard groups expressed as layout routes: the wrapper renders once and
 * children mount via <Outlet/>. A new admin page added to the manifest
 * under guard:'admin' can't accidentally skip the permission check.
 */
export function RequireAuth() {
  return (
    <ProtectedRoute>
      <Outlet />
    </ProtectedRoute>
  );
}

// Fallback shown while a code-split admin chunk loads. Matches the plain
// "Loading…" the guards already use, so the transition is visually quiet.
const ChunkFallback = <div className="container mx-auto py-8 text-muted-foreground">Loading…</div>;

export function RequireConfirmed() {
  // The /admin/audit route's element is lazy, so the confirmed group needs a
  // Suspense boundary too (not just the admin group).
  return (
    <ProtectedRoute requireConfirmed>
      <Suspense fallback={ChunkFallback}>
        <Outlet />
      </Suspense>
    </ProtectedRoute>
  );
}

export function RequireAdmin() {
  return (
    <ProtectedRoute requirePermission={Permission.Administer} requireConfirmed>
      <Suspense fallback={ChunkFallback}>
        <Outlet />
      </Suspense>
    </ProtectedRoute>
  );
}

/**
 * A data router catches render errors itself and shows its own screen; hand
 * them on to the app's ErrorBoundary instead, as the old <BrowserRouter> did.
 */
export function RethrowRouteError(): never {
  throw useRouteError();
}
