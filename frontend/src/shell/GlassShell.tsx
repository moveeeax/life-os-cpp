import { Suspense } from 'react';
import { Outlet, useLocation } from 'react-router';

import '@fontsource-variable/outfit';

import { Backdrop } from './Backdrop';
import { GlassAccountMenu } from './GlassAccountMenu';
import { GlassNav } from './GlassNav';
import { HideNumbersButton } from './HideNumbersButton';
import './glass.css';

/**
 * The signed-in app: one picture behind every screen (open on Now, dimmed
 * elsewhere), the glass pill nav, and the screen. A section not redesigned
 * yet sits on one large glass sheet in its dark look.
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
          ) : (
            <div className="g-page">
              <div className="g-sheet g-legacy" data-legacy-sheet>
                <Outlet />
              </div>
            </div>
          )}
        </Suspense>
      </main>
      <HideNumbersButton />
    </div>
  );
}
