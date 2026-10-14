import { useEffect, useRef } from 'react';
import { NavLink, useLocation } from 'react-router';

import { useGlassNav } from './useGlassNav';

/** The glass pill nav; a section whose module is off is left out. */
export function GlassNav() {
  const { items } = useGlassNav();
  const { pathname } = useLocation();
  const ref = useRef<HTMLElement>(null);
  // On a phone the pill scrolls sideways: keep the current section in view.
  useEffect(() => {
    const current = ref.current?.querySelector<HTMLElement>('[aria-current="page"]');
    current?.scrollIntoView?.({ block: 'nearest', inline: 'nearest' });
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
