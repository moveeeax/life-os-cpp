import { Suspense } from 'react';
import { Outlet, useLocation } from 'react-router';

import '@fontsource-variable/outfit';

import { Backdrop } from './Backdrop';
import { GlassAccountMenu } from './GlassAccountMenu';
import { GlassNav } from './GlassNav';
import { isCalm } from './calm';
import { HideNumbersButton } from './HideNumbersButton';
import './glass.css';
import './kit/kit.css';

/**
 * The signed-in app: one picture behind every screen (open on Now, dimmed
 * elsewhere), the glass pill nav, and the screen. A section on the calm
 * sheet (calm.ts) renders its own column; one not moved yet sits on one large
 * glass sheet in its dark look.
 */
export function GlassShell() {
  const { pathname } = useLocation();
  const onNow = pathname === '/now';
  return (
    <div className="glass dark font-outfit" data-open={onNow ? '' : undefined}>
      <a
        href="#main-content"
        className="sr-only focus:not-sr-only focus:absolute focus:inset-s-4 focus:top-4 focus:z-[60] focus:rounded-lg focus:bg-white focus:px-3 focus:py-2 focus:text-sm focus:font-medium focus:text-gray-800"
      >
        Skip to main content
      </a>
      <Backdrop />
      <div className="g-shade" />
      <div className="g-bar">
        <GlassNav />
        <GlassAccountMenu />
      </div>
      <main id="main-content" className="g-main">
        <Suspense fallback={null}>
          {onNow ? (
            <Outlet />
          ) : isCalm(pathname) ? (
            <div className="g-page">
              <Outlet />
            </div>
          ) : (
            <div className="g-page">
              <div className="g-sheet g-legacy" data-legacy-sheet>
                <Outlet />
              </div>
            </div>
          )}
        </Suspense>
      </main>
      {/* Dialogs land here: inside .glass, outside the sheets and their filters. */}
      <div id="g-portal" />
      <HideNumbersButton />
    </div>
  );
}
