import { createContext, useContext, useEffect, useState, type ReactNode } from 'react';

// Sidebar state, after TailAdmin's SidebarContext (MIT): expanded or rail on
// desktop, a drawer below the xl breakpoint, and hover-to-expand for the rail.
interface SidebarState {
  isExpanded: boolean;
  isMobileOpen: boolean;
  isHovered: boolean;
  toggleSidebar: () => void;
  toggleMobileSidebar: () => void;
  setIsHovered: (hovered: boolean) => void;
  setIsMobileOpen: (open: boolean) => void;
}

const SidebarContext = createContext<SidebarState | undefined>(undefined);

/** Viewports narrower than this use the drawer (Tailwind's xl breakpoint). */
export const DESKTOP_MIN_WIDTH = 1280;

// eslint-disable-next-line react-refresh/only-export-components
export function useSidebar(): SidebarState {
  const ctx = useContext(SidebarContext);
  if (!ctx) throw new Error('useSidebar must be used within a SidebarProvider');
  return ctx;
}

export function SidebarProvider({ children }: { children: ReactNode }) {
  const [isExpanded, setIsExpanded] = useState(true);
  const [isMobileOpen, setIsMobileOpen] = useState(false);
  const [isMobile, setIsMobile] = useState(false);
  const [isHovered, setIsHovered] = useState(false);

  useEffect(() => {
    const onResize = () => {
      const mobile = window.innerWidth < DESKTOP_MIN_WIDTH;
      setIsMobile(mobile);
      if (!mobile) setIsMobileOpen(false);
    };
    onResize();
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  return (
    <SidebarContext.Provider
      value={{
        isExpanded: isMobile ? false : isExpanded,
        isMobileOpen,
        isHovered,
        toggleSidebar: () => setIsExpanded((v) => !v),
        toggleMobileSidebar: () => setIsMobileOpen((v) => !v),
        setIsHovered,
        setIsMobileOpen,
      }}
    >
      {children}
    </SidebarContext.Provider>
  );
}
