import { Suspense } from 'react';
import { Routes, Route, Outlet } from 'react-router';

import { Layout } from '@/components/Layout';
import { ProtectedRoute } from '@/components/ProtectedRoute';
import { NotFoundPage } from '@/pages/NotFound';
import { Permission } from '@/lib/auth/permissions';
import { routes, type RouteEntry, type RouteGuard, type RouteLayout } from '@/routes/manifest';
import { AppShell } from '@/shell/AppShell';

/**
 * Guard groups expressed as layout routes: the wrapper renders once and
 * children mount via <Outlet/>. A new admin page added to the manifest
 * under guard:'admin' can't accidentally skip the permission check.
 */
function RequireAuth() {
  return (
    <ProtectedRoute>
      <Outlet />
    </ProtectedRoute>
  );
}

// Fallback shown while a code-split admin chunk loads. Matches the plain
// "Loading…" the guards already use, so the transition is visually quiet.
const ChunkFallback = <div className="container mx-auto py-8 text-muted-foreground">Loading…</div>;

function RequireConfirmed() {
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

function RequireAdmin() {
  return (
    <ProtectedRoute requirePermission={Permission.Administer} requireConfirmed>
      <Suspense fallback={ChunkFallback}>
        <Outlet />
      </Suspense>
    </ProtectedRoute>
  );
}

function routesFor(guard: RouteGuard, layout: RouteLayout = 'classic'): RouteEntry[] {
  return routes.filter((r) => r.guard === guard && (r.layout ?? 'classic') === layout);
}

function renderRoute(r: RouteEntry) {
  return (
    <Route
      key={r.path}
      path={r.path}
      element={
        r.requirePermission !== undefined && r.guard !== 'admin' ? (
          <ProtectedRoute requirePermission={r.requirePermission} requireConfirmed>
            {r.element}
          </ProtectedRoute>
        ) : (
          r.element
        )
      }
    />
  );
}

export default function App() {
  return (
    <Routes>
      {/* No frame: the full-screen sign-in page and its /login alias. */}
      {routesFor('public', 'bare').map(renderRoute)}

      {/* Dashboard shell: sidebar + header, signed-in and confirmed users. */}
      {/* The guard wraps the frame, so the sidebar never renders for a guest. */}
      <Route element={<RequireConfirmed />}>
        <Route element={<AppShell />}>{routesFor('confirmed', 'shell').map(renderRoute)}</Route>
      </Route>

      <Route element={<Layout />}>
        {/* Public pages */}
        {routesFor('public').map(renderRoute)}

        {/* Authenticated but possibly unconfirmed */}
        <Route element={<RequireAuth />}>{routesFor('auth').map(renderRoute)}</Route>

        {/* Authenticated + confirmed */}
        <Route element={<RequireConfirmed />}>{routesFor('confirmed').map(renderRoute)}</Route>

        {/* Admin — gated by Permission.Administer (0x40000000 sentinel) */}
        <Route element={<RequireAdmin />}>{routesFor('admin').map(renderRoute)}</Route>

        <Route path="*" element={<NotFoundPage />} />
      </Route>
    </Routes>
  );
}
