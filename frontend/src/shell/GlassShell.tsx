import { Suspense } from 'react';
import { Outlet, useLocation } from 'react-router';

import '@fontsource-variable/outfit';

import { Backdrop } from './Backdrop';
import { GlassAccountMenu } from './GlassAccountMenu';
import { GlassNav } from './GlassNav';
import { HideNumbersButton } from './HideNumbersButton';
import { SEARCH_OPEN, SearchPalette } from './search/SearchPalette';
import './glass.css';
import './kit/kit.css';

/**
 * The signed-in app: one picture behind every screen (open on Now, dimmed
 * elsewhere), the glass pill nav, and the screen. Each section renders its
 * own calm sheet in the centred column (shell/kit).
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
        <div className="g-account">
          <button
            type="button"
            className="g-pill"
            aria-label="Search (⌘K)"
            title="Search (⌘K)"
            onClick={() => window.dispatchEvent(new Event(SEARCH_OPEN))}
          >
            <svg viewBox="0 0 20 20" width="17" height="17" aria-hidden="true">
              <circle
                cx="8.5"
                cy="8.5"
                r="5.5"
                fill="none"
                stroke="currentColor"
                strokeWidth="1.7"
              />
              <path d="m13 13 4 4" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" />
            </svg>
          </button>
        </div>
        <GlassAccountMenu />
      </div>
      <main id="main-content" className="g-main">
        <Suspense fallback={null}>
          {onNow ? (
            <Outlet />
          ) : (
            <div className="g-page">
              <Outlet />
            </div>
          )}
        </Suspense>
      </main>
      {/* Dialogs land here: inside .glass, outside the sheets and their filters. */}
      <div id="g-portal" />
      <SearchPalette />
      <HideNumbersButton />
    </div>
  );
}
