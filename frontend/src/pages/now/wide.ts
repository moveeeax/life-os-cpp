import { useEffect, useState } from 'react';

/** From this width the shell keeps Now under every tab. */
export const KEEP_FROM = 1024;

/**
 * Whether the shell keeps Now under every tab: on a wide screen unless the Now
 * module is known to be off. While the modules probe is still out (undefined)
 * the shell already owns Now, so the /now route never draws a first copy that
 * is torn down a moment later (two mounts, every Now read sent twice).
 */
export function keepsNow(wide: boolean, nowOn: boolean | undefined): boolean {
  return wide && nowOn !== false;
}

/** True while the window is at least this wide. */
export function useWide(px: number): boolean {
  const query = `(min-width: ${px}px)`;
  const [wide, setWide] = useState(
    () => typeof matchMedia === 'function' && matchMedia(query).matches,
  );
  useEffect(() => {
    if (typeof matchMedia !== 'function') return;
    const m = matchMedia(query);
    const on = () => setWide(m.matches);
    m.addEventListener('change', on);
    return () => m.removeEventListener('change', on);
  }, [query]);
  return wide;
}
