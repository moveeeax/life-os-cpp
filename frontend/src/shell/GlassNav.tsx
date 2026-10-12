import { NavLink } from 'react-router';

import { useGlassNav } from './useGlassNav';

/** The glass pill nav; a section whose module is off is left out. */
export function GlassNav() {
  const { items } = useGlassNav();
  return (
    <nav className="g-pill g-nav" aria-label="Life OS">
      {items.map((s) => (
        <NavLink key={s.key} to={s.to} end={s.to === '/now'}>
          {s.label}
        </NavLink>
      ))}
    </nav>
  );
}
