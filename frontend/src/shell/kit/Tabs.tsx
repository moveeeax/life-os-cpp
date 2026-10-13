import { useEffect, useRef, useState } from 'react';
import { NavLink, useLocation } from 'react-router';

import { splitTabs, type TabItem } from './logic';

function active(pathname: string, t: TabItem): boolean {
  return t.end ? pathname === t.to : pathname === t.to || pathname.startsWith(`${t.to}/`);
}

/** Underline tabs; more than @p max fold into "More ▾". */
export function Tabs({ items, max = 5, label }: { items: TabItem[]; max?: number; label: string }) {
  const { pathname } = useLocation();
  const { shown, more } = splitTabs(items, max);
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  useEffect(() => setOpen(false), [pathname]);
  useEffect(() => {
    if (!open) return;
    const away = (e: MouseEvent) => {
      if (!box.current?.contains(e.target as Node)) setOpen(false);
    };
    const esc = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false);
    document.addEventListener('mousedown', away);
    document.addEventListener('keydown', esc);
    return () => {
      document.removeEventListener('mousedown', away);
      document.removeEventListener('keydown', esc);
    };
  }, [open]);
  const inMore = more.find((t) => active(pathname, t));
  const link = (t: TabItem) => (
    <NavLink
      key={t.to}
      to={t.to}
      end={t.end}
      className={({ isActive }) => (isActive ? 'k-tab k-on' : 'k-tab')}
    >
      {t.label}
      {t.count !== undefined && t.count > 0 && <span className="k-count">{t.count}</span>}
    </NavLink>
  );
  return (
    <nav className="k-tabs" aria-label={label}>
      {shown.map(link)}
      {more.length > 0 && (
        <div className="k-more" ref={box}>
          <button
            type="button"
            className={inMore ? 'k-tab k-on' : 'k-tab'}
            aria-haspopup="menu"
            aria-expanded={open}
            onClick={() => setOpen((v) => !v)}
          >
            {inMore ? inMore.label : 'More'} ▾
          </button>
          {open && (
            <div className="k-menu" role="menu">
              {more.map((t) => (
                <NavLink key={t.to} to={t.to} end={t.end} role="menuitem" className="k-menu-item">
                  {t.label}
                </NavLink>
              ))}
            </div>
          )}
        </div>
      )}
    </nav>
  );
}
