import { Outlet } from 'react-router';

import '@fontsource-variable/outfit';

import { cn } from '@/lib/utils';

import { AppHeader } from './AppHeader';
import { AppSidebar } from './AppSidebar';
import { SidebarProvider, useSidebar } from './SidebarContext';

function Backdrop() {
  const { isMobileOpen, toggleMobileSidebar } = useSidebar();
  if (!isMobileOpen) return null;
  return (
    <div
      className="fixed inset-0 z-40 bg-gray-900/50 xl:hidden"
      onClick={toggleMobileSidebar}
      aria-hidden="true"
    />
  );
}

function ShellContent() {
  const { isExpanded, isHovered } = useSidebar();
  return (
    <div className="min-h-screen bg-gray-50 font-outfit text-gray-800 xl:flex dark:bg-gray-950 dark:text-white/90">
      <a
        href="#main-content"
        className="sr-only focus:not-sr-only focus:absolute focus:inset-s-4 focus:top-4 focus:z-[60] focus:rounded-lg focus:bg-white focus:px-3 focus:py-2 focus:text-sm focus:font-medium focus:text-gray-800 focus:shadow-theme-md"
      >
        Skip to main content
      </a>
      <AppSidebar />
      <Backdrop />
      <div
        className={cn(
          'flex-1 transition-[margin] duration-300 ease-in-out',
          isExpanded || isHovered ? 'xl:ms-72.5' : 'xl:ms-22.5',
        )}
      >
        <AppHeader />
        <main id="main-content" className="mx-auto max-w-(--breakpoint-2xl) p-4 md:p-6">
          <Outlet />
        </main>
      </div>
    </div>
  );
}

/**
 * Dashboard shell: sidebar, header and the routed section. Layout after
 * TailAdmin's AppLayout (MIT). The Outfit font applies inside the shell only.
 */
export function AppShell() {
  return (
    <SidebarProvider>
      <ShellContent />
    </SidebarProvider>
  );
}
