import { useState } from 'react';

/**
 * Light/dark toggle. The `.dark` class on <html> drives Tailwind's `dark:`
 * variants; the initial class is set before first paint by /theme.js. The
 * choice is kept in localStorage under `theme`.
 */
export function useThemeToggle(): { dark: boolean; toggleTheme: () => void } {
  const [dark, setDark] = useState(
    () => typeof document !== 'undefined' && document.documentElement.classList.contains('dark'),
  );
  const toggleTheme = () => {
    const next = !dark;
    setDark(next);
    document.documentElement.classList.toggle('dark', next);
    try {
      localStorage.setItem('theme', next ? 'dark' : 'light');
    } catch {
      /* ignore */
    }
  };
  return { dark, toggleTheme };
}
