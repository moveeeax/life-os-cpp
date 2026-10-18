import { useEffect, useRef, useState } from 'react';
import { NavLink, useLocation } from 'react-router';

import { useGlassNav } from './useGlassNav';

/** Which edge of the pill hides more sections. */
type More = 'none' | 'left' | 'right' | 'both';

function moreOf(nav: HTMLElement): More {
  const left = nav.scrollLeft > 1;
  const right = nav.scrollLeft + nav.clientWidth < nav.scrollWidth - 1;
  if (left && right) return 'both';
  if (left) return 'left';
  if (right) return 'right';
  return 'none';
}

/**
 * The glass pill nav; a section whose module is off is left out. On a phone
 * the pill scrolls sideways (only the pill, never the page): the current
 * section is kept in view and the edge that hides more fades out.
 */
export function GlassNav() {
  const { items } = useGlassNav();
  const { pathname } = useLocation();
  const ref = useRef<HTMLElement>(null);
  const [more, setMore] = useState<More>('none');
  useEffect(() => {
    const nav = ref.current;
    const current = nav?.querySelector<HTMLElement>('[aria-current="page"]');
    if (!nav || !current) return;
    const a = current.getBoundingClientRect();
    const n = nav.getBoundingClientRect();
    if (a.left < n.left) nav.scrollLeft -= n.left - a.left + 8;
    else if (a.right > n.right) nav.scrollLeft += a.right - n.right + 8;
    setMore(moreOf(nav));
  }, [pathname, items.length]);
  useEffect(() => {
    const nav = ref.current;
    if (!nav) return;
    const update = () => setMore(moreOf(nav));
    nav.addEventListener('scroll', update, { passive: true });
    window.addEventListener('resize', update);
    return () => {
      nav.removeEventListener('scroll', update);
      window.removeEventListener('resize', update);
    };
  }, []);
  return (
    <div className="g-pill g-navwrap">
      <nav ref={ref} className="g-nav" aria-label="Life OS" data-more={more}>
        {items.map((s) => (
          <NavLink key={s.key} to={s.to} end={s.to === '/now'}>
            {s.label}
          </NavLink>
        ))}
      </nav>
    </div>
  );
}
