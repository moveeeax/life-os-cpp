import { useEffect } from 'react';
import { Link, useLocation } from 'react-router';
import { Ellipsis, Shield, X } from 'lucide-react';

import { useMe } from '@/hooks/useMe';
import { useWorkoutEnabled } from '@/hooks/useWorkoutSession';
import { Permission, userCan, userIsAdmin } from '@/lib/auth/permissions';
import { BRAND } from '@/lib/brand';
import { cn } from '@/lib/utils';

import { DASHBOARD_HOME, visibleSections, type Section } from './sections';
import { useSidebar } from './SidebarContext';

// Layout and classes after TailAdmin's AppSidebar (MIT), reduced to a flat
// list: no submenus, no i18n, lucide icons instead of bundled SVGs.
export function AppSidebar() {
  const { isExpanded, isMobile, isMobileOpen, isHovered, setIsHovered, setIsMobileOpen } =
    useSidebar();
  const location = useLocation();
  const user = useMe().data ?? null;
  const wide = isExpanded || isHovered || isMobileOpen;
  // A section whose backend module is switched off stays a "soon" item.
  const workoutOn = useWorkoutEnabled(userCan(user, Permission.FitnessRead));
  const off = workoutOn ? undefined : new Set(['workout']);

  // Close the drawer after a navigation on small screens.
  useEffect(() => {
    setIsMobileOpen(false);
  }, [location.pathname, setIsMobileOpen]);

  // Escape closes the drawer.
  useEffect(() => {
    if (!isMobileOpen) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setIsMobileOpen(false);
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [isMobileOpen, setIsMobileOpen]);

  // A closed drawer is only moved off-screen; `inert` keeps its links out of
  // the tab order and away from screen readers until it opens.
  const closedDrawer = isMobile && !isMobileOpen;

  const isActive = (path: string) =>
    location.pathname === path || location.pathname.startsWith(`${path}/`);

  const renderItem = (s: Section) => {
    const Icon = s.icon;
    if (!s.path) {
      return (
        <li key={s.key}>
          <span
            className={cn('menu-item menu-item-disabled', !wide && 'xl:justify-center')}
            title={`${s.label} (soon)`}
          >
            <Icon className="size-6 shrink-0" aria-hidden="true" />
            {wide && (
              <>
                <span>{s.label}</span>
                <span className="ms-auto rounded-full bg-gray-100 px-2 py-0.5 text-theme-xs font-medium text-gray-500 dark:bg-white/5 dark:text-gray-400">
                  soon
                </span>
              </>
            )}
          </span>
        </li>
      );
    }
    const active = isActive(s.path);
    return (
      <li key={s.key}>
        <Link
          to={s.path}
          aria-label={s.label}
          aria-current={active ? 'page' : undefined}
          className={cn(
            'group menu-item',
            active ? 'menu-item-active' : 'menu-item-inactive',
            !wide && 'xl:justify-center',
          )}
        >
          <Icon
            className={cn(
              'size-6 shrink-0',
              active ? 'menu-item-icon-active' : 'menu-item-icon-inactive',
            )}
            aria-hidden="true"
          />
          {wide && <span>{s.label}</span>}
        </Link>
      </li>
    );
  };

  return (
    <aside
      className={cn(
        'fixed inset-s-0 top-0 z-50 flex h-screen flex-col border-e border-gray-200 bg-white px-5 text-gray-900 transition-all duration-300 ease-in-out xl:translate-x-0 dark:border-gray-800 dark:bg-gray-900',
        wide ? 'w-72.5' : 'w-22.5',
        isMobileOpen ? 'translate-x-0' : '-translate-x-full',
      )}
      inert={closedDrawer}
      onMouseEnter={() => !isExpanded && setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
      onFocus={() => !isExpanded && !isMobile && setIsHovered(true)}
      onBlur={() => setIsHovered(false)}
    >
      <div className={cn('flex items-center py-8', wide ? 'justify-between' : 'xl:justify-center')}>
        <Link
          to={DASHBOARD_HOME}
          className="text-theme-xl font-semibold text-gray-800 dark:text-white/90"
        >
          {wide ? BRAND : BRAND.slice(0, 1)}
        </Link>
        {isMobileOpen && (
          <button
            type="button"
            onClick={() => setIsMobileOpen(false)}
            aria-label="Close menu"
            className="flex h-10 w-10 items-center justify-center rounded-lg text-gray-500 hover:bg-gray-100 dark:text-gray-400 dark:hover:bg-white/5"
          >
            <X className="size-6" aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="no-scrollbar flex flex-1 flex-col overflow-y-auto duration-300 ease-linear">
        <nav className="mb-6" aria-label="Sections">
          <h2
            className={cn(
              'mb-4 flex text-xs leading-5 text-gray-400 uppercase',
              wide ? 'justify-start' : 'xl:justify-center',
            )}
          >
            {wide ? 'Menu' : <Ellipsis className="size-6" aria-hidden="true" />}
          </h2>
          <ul className="flex flex-col gap-1">{visibleSections(user, off).map(renderItem)}</ul>
        </nav>

        {userIsAdmin(user) && (
          <nav className="mt-auto mb-6" aria-label="Administration">
            <ul className="flex flex-col gap-1">
              <li>
                <Link
                  to="/admin"
                  aria-label="Admin"
                  className={cn('group menu-item menu-item-inactive', !wide && 'xl:justify-center')}
                >
                  <Shield className="menu-item-icon-inactive size-6 shrink-0" aria-hidden="true" />
                  {wide && <span>Admin</span>}
                </Link>
              </li>
            </ul>
          </nav>
        )}
      </div>
    </aside>
  );
}
