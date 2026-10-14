import { Route, createRoutesFromElements } from 'react-router';

import { Layout } from '@/components/Layout';
import { ProtectedRoute } from '@/components/ProtectedRoute';
import {
  RequireAdmin,
  RequireAuth,
  RequireConfirmed,
  RethrowRouteError,
} from '@/components/RouteGuards';
import { NotFoundPage } from '@/pages/NotFound';
import { routes, type RouteEntry, type RouteGuard, type RouteLayout } from '@/routes/manifest';
import { GlassShell } from '@/shell/GlassShell';

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

/**
 * The route tree for createBrowserRouter (main.tsx). A data router, so pages
 * can use useBlocker; the manifest still drives every route.
 */
export const appRoutes = createRoutesFromElements(
  <Route errorElement={<RethrowRouteError />}>
    {/* No frame: the full-screen sign-in page and its /login alias. */}
    {routesFor('public', 'bare').map(renderRoute)}

    {/* The glass shell: the picture, the pill nav and the screen; signed-in and confirmed users. */}
    {/* The guard wraps the frame, so the shell never renders for a guest. */}
    <Route element={<RequireConfirmed />}>
      <Route element={<GlassShell />}>{routesFor('confirmed', 'shell').map(renderRoute)}</Route>
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
  </Route>,
);
