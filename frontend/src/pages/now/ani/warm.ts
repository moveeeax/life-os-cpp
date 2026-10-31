import { useEffect, useState } from 'react';

/** Her own lines start reading the site this long after the page opens. */
export const WARM_MS = 60_000;
/** ...one read after another, this far apart, not in one burst (the 429 of 2026-11-01). */
export const WARM_STEP_MS = 5_000;

/** True once @p on has held for WARM_MS plus @p step steps. */
export function useWarm(on: boolean, step: number): boolean {
  const [warm, setWarm] = useState(false);
  useEffect(() => {
    if (!on) {
      setWarm(false);
      return;
    }
    const t = window.setTimeout(() => setWarm(true), WARM_MS + step * WARM_STEP_MS);
    return () => clearTimeout(t);
  }, [on, step]);
  return warm;
}
