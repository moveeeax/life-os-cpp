import { useEffect, useRef } from 'react';
import { NavLink, useLocation } from 'react-router';

import { useGlassNav } from './useGlassNav';

/** The glass pill nav; a section whose module is off is left out. */
export function GlassNav() {
  const { items } = useGlassNav();
  const { pathname } = useLocation();
  const ref = useRef<HTMLElement>(null);
  // On a phone the pill scrolls sideways: keep the current section in view.
  // Only the pill scrolls, never the page.
  useEffect(() => {
    const nav = ref.current;
    const current = nav?.querySelector<HTMLElement>('[aria-current="page"]');
    if (!nav || !current) return;
    const a = current.getBoundingClientRect();
    const n = nav.getBoundingClientRect();
    if (a.left < n.left) nav.scrollLeft -= n.left - a.left + 8;
    else if (a.right > n.right) nav.scrollLeft += a.right - n.right + 8;
  }, [pathname, items.length]);
  return (
    <nav ref={ref} className="g-pill g-nav" aria-label="Life OS">
      {items.map((s) => (
        <NavLink key={s.key} to={s.to} end={s.to === '/now'}>
          {s.label}
        </NavLink>
      ))}
    </nav>
  );
}
