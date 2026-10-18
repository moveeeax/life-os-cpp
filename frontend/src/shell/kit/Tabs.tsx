import {
  useEffect,
  useId,
  useRef,
  useState,
  type FocusEvent as ReactFocusEvent,
  type KeyboardEvent as ReactKeyboardEvent,
} from 'react';
import { NavLink, useLocation } from 'react-router';

import { splitTabs, type TabItem } from './logic';

function active(pathname: string, t: TabItem): boolean {
  return t.end ? pathname === t.to : pathname === t.to || pathname.startsWith(`${t.to}/`);
}

/**
 * Underline tabs; more than @p max fold into "More ▾", a menu the keyboard
 * can walk: ArrowDown or ArrowUp on the button opens it on the first or the
 * last item, the arrows move with wrap, Home and End jump, Escape closes and
 * returns the focus to the button, Tab leaves and closes. Focus that moves
 * anywhere outside the menu (a screen reader, a click elsewhere) closes it too.
 */
export function Tabs({ items, max = 5, label }: { items: TabItem[]; max?: number; label: string }) {
  const { pathname } = useLocation();
  const { shown, more } = splitTabs(items, max);
  const [open, setOpen] = useState(false);
  // Which item takes the focus once the menu is in the DOM.
  const [focusAt, setFocusAt] = useState<'first' | 'last' | null>(null);
  const box = useRef<HTMLDivElement>(null);
  const button = useRef<HTMLButtonElement>(null);
  const menu = useRef<HTMLDivElement>(null);
  const menuId = useId();
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
  useEffect(() => {
    if (!open || !focusAt) return;
    const links = menu.current?.querySelectorAll<HTMLElement>('[role="menuitem"]');
    if (!links?.length) return;
    links[focusAt === 'first' ? 0 : links.length - 1].focus();
    setFocusAt(null);
  }, [open, focusAt]);

  const openAt = (at: 'first' | 'last') => {
    setFocusAt(at);
    setOpen(true);
  };
  const close = (back: boolean) => {
    setOpen(false);
    if (back) button.current?.focus();
  };
  const onButtonKey = (e: ReactKeyboardEvent<HTMLButtonElement>) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      openAt('first');
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      openAt('last');
    }
  };
  const onMenuKey = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    const links = [...(menu.current?.querySelectorAll<HTMLElement>('[role="menuitem"]') ?? [])];
    const i = links.indexOf(document.activeElement as HTMLElement);
    const go = (j: number) => {
      e.preventDefault();
      links[(j + links.length) % links.length]?.focus();
    };
    if (e.key === 'ArrowDown') go(i + 1);
    else if (e.key === 'ArrowUp') go(i - 1);
    else if (e.key === 'Home') go(0);
    else if (e.key === 'End') go(links.length - 1);
    else if (e.key === 'Escape') {
      e.preventDefault();
      close(true);
    } else if (e.key === 'Tab') close(false);
  };

  // Focus gone to an element outside the box closes the menu. A null target
  // (a click on nothing, or Safari not focusing a clicked link) is left to the
  // mousedown handler, so a click on an item still lands.
  const onBoxBlur = (e: ReactFocusEvent<HTMLDivElement>) => {
    const to = e.relatedTarget as Node | null;
    if (to && !box.current?.contains(to)) setOpen(false);
  };

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
        <div className="k-more" ref={box} onBlur={onBoxBlur}>
          <button
            ref={button}
            type="button"
            className={inMore ? 'k-tab k-on' : 'k-tab'}
            aria-expanded={open}
            aria-haspopup="menu"
            aria-controls={open ? menuId : undefined}
            onClick={() => setOpen((v) => !v)}
            onKeyDown={onButtonKey}
          >
            {inMore ? inMore.label : 'More'} ▾
          </button>
          {open && (
            <div id={menuId} className="k-menu" role="menu" ref={menu} onKeyDown={onMenuKey}>
              {more.map((t) => (
                <NavLink
                  key={t.to}
                  to={t.to}
                  end={t.end}
                  className="k-menu-item"
                  role="menuitem"
                  tabIndex={-1}
                >
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
