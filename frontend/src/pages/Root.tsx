import { Navigate, useLocation } from 'react-router';

import { useMe } from '@/hooks/useMe';
import { DASHBOARD_HOME, rootTarget } from '@/shell/sections';

import { SignInPage } from './SignIn';

/** `/`: the sign-in form for a guest, the dashboard for a signed-in user. */
export function RootPage() {
  const me = useMe();
  const target = rootTarget(me);
  // The same quiet "Loading…" the route guards show, not a blank page.
  if (target === 'loading')
    return <div className="container mx-auto py-8 text-muted-foreground">Loading…</div>;
  if (target === 'dashboard') return <Navigate to={DASHBOARD_HOME} replace />;
  return <SignInPage />;
}

/** `/login` is kept for old links and guard redirects; it lands on `/`. */
export function LoginRedirect() {
  const location = useLocation();
  return <Navigate to="/" replace state={location.state} />;
}
