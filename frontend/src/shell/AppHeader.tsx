import { useEffect, useRef, useState } from 'react';
import { Link, useNavigate } from 'react-router';
import { ChevronDown, LogOut, Menu, Moon, Sun, User, X } from 'lucide-react';

import { useToast } from '@/components/ui/toaster';
import { useLogout } from '@/hooks/useAuthMutations';
import { useMe } from '@/hooks/useMe';
import { useThemeToggle } from '@/hooks/useThemeToggle';
import { BRAND } from '@/lib/brand';
import { cn } from '@/lib/utils';

import { DASHBOARD_HOME } from './sections';
import { DESKTOP_MIN_WIDTH, useSidebar } from './SidebarContext';

const menuItemClass =
  'group flex w-full items-center gap-3 rounded-lg px-3 py-2 text-theme-sm font-medium text-gray-700 hover:bg-gray-100 hover:text-gray-700 dark:text-gray-400 dark:hover:bg-white/5 dark:hover:text-gray-300';

function UserMenu() {
  const user = useMe().data ?? null;
  const logout = useLogout();
  const toast = useToast();
  const navigate = useNavigate();
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  const buttonRef = useRef<HTMLButtonElement>(null);

  // Close on outside click and on Escape.
  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setOpen(false);
        buttonRef.current?.focus();
      }
    };
    document.addEventListener('mousedown', onDown);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('mousedown', onDown);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  if (!user) return null;
  const name = user.full_name || user.email;

  const signOut = async () => {
    setOpen(false);
    try {
      await logout.mutateAsync();
      navigate('/', { replace: true });
    } catch {
      toast.error('Could not sign out. Check the connection and try again.');
    }
  };

  return (
    <div
      className="relative"
      ref={ref}
      onBlur={(e) => {
        // Tabbing out of the panel closes it.
        if (!e.currentTarget.contains(e.relatedTarget)) setOpen(false);
      }}
    >
      <button
        ref={buttonRef}
        type="button"
        onClick={() => setOpen((v) => !v)}
        aria-expanded={open}
        aria-controls="user-menu"
        className="flex items-center text-gray-700 dark:text-gray-400"
      >
        <span className="me-3 flex h-11 w-11 items-center justify-center rounded-full bg-brand-50 text-theme-sm font-semibold text-brand-500 dark:bg-brand-500/[0.12] dark:text-brand-400">
          {name.slice(0, 1).toUpperCase()}
        </span>
        <span className="me-1 hidden max-w-40 truncate text-theme-sm font-medium sm:block">
          {name}
        </span>
        <ChevronDown
          className={cn('size-4.5 transition-transform duration-200', open && 'rotate-180')}
          aria-hidden="true"
        />
      </button>

      {open && (
        <div
          id="user-menu"
          className="absolute inset-e-0 z-50 mt-4.25 flex w-65 flex-col rounded-2xl border border-gray-200 bg-white p-3 shadow-theme-lg dark:border-gray-800 dark:bg-gray-dark"
        >
          <div className="border-b border-gray-200 px-3 pb-3 dark:border-gray-800">
            <span className="block truncate text-theme-sm font-medium text-gray-700 dark:text-gray-400">
              {name}
            </span>
            <span className="mt-0.5 block truncate text-theme-xs text-gray-500 dark:text-gray-400">
              {user.email}
            </span>
          </div>
          <ul className="flex flex-col gap-1 pt-3">
            <li>
              <Link to="/account" className={menuItemClass} onClick={() => setOpen(false)}>
                <User className="size-5" aria-hidden="true" />
                Profile
              </Link>
            </li>
            <li>
              <button type="button" className={menuItemClass} onClick={signOut}>
                <LogOut className="size-5" aria-hidden="true" />
                Sign out
              </button>
            </li>
          </ul>
        </div>
      )}
    </div>
  );
}

// Layout and classes after TailAdmin's AppHeader (MIT), without search and
// notifications.
export function AppHeader() {
  const { isExpanded, isMobile, isMobileOpen, toggleSidebar, toggleMobileSidebar } = useSidebar();
  const { dark, toggleTheme } = useThemeToggle();

  const onToggle = () => {
    if (window.innerWidth >= DESKTOP_MIN_WIDTH) toggleSidebar();
    else toggleMobileSidebar();
  };

  return (
    <header className="sticky top-0 z-30 flex w-full border-b border-gray-200 bg-white dark:border-gray-800 dark:bg-gray-900">
      <div className="flex grow items-center justify-between gap-2 px-3 py-3 sm:gap-4 xl:px-6 xl:py-4">
        <div className="flex items-center gap-2 sm:gap-4">
          <button
            type="button"
            onClick={onToggle}
            aria-label="Toggle sidebar"
            aria-expanded={isMobile ? isMobileOpen : isExpanded}
            className={cn(
              'flex h-10 w-10 items-center justify-center rounded-lg border-gray-200 text-gray-500 lg:h-11 lg:w-11 xl:border dark:border-gray-800 dark:text-gray-400',
              isMobileOpen && 'bg-gray-100 dark:bg-white/3',
            )}
          >
            {isMobileOpen ? (
              <X className="size-6" aria-hidden="true" />
            ) : (
              <Menu className="size-5" aria-hidden="true" />
            )}
          </button>
          <Link
            to={DASHBOARD_HOME}
            className="text-theme-xl font-semibold text-gray-800 xl:hidden dark:text-white/90"
          >
            {BRAND}
          </Link>
        </div>

        <div className="flex items-center gap-3">
          <button
            type="button"
            onClick={toggleTheme}
            aria-label={dark ? 'Switch to light theme' : 'Switch to dark theme'}
            className="relative flex h-11 w-11 items-center justify-center rounded-full border border-gray-200 bg-white text-gray-500 transition-colors hover:bg-gray-100 hover:text-gray-700 dark:border-gray-800 dark:bg-gray-900 dark:text-gray-400 dark:hover:bg-gray-800 dark:hover:text-white"
          >
            {dark ? (
              <Sun className="size-5" aria-hidden="true" />
            ) : (
              <Moon className="size-5" aria-hidden="true" />
            )}
          </button>
          <UserMenu />
        </div>
      </div>
    </header>
  );
}
