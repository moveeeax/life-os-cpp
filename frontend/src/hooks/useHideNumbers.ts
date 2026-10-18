import { useCallback, useSyncExternalStore } from 'react';

const KEY = 'hide-numbers';
const CLASS = 'hide-numbers';

function stored(): boolean {
  try {
    return globalThis.localStorage?.getItem(KEY) === '1';
  } catch {
    return false;
  }
}

// One state for the whole page: the bar pill and the account menu read and
// flip the same value, so they never disagree.
let hidden = stored();
const listeners = new Set<() => void>();
if (typeof document !== 'undefined') document.documentElement.classList.toggle(CLASS, hidden);

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function apply(v: boolean) {
  hidden = v;
  document.documentElement.classList.toggle(CLASS, v);
  try {
    globalThis.localStorage?.setItem(KEY, v ? '1' : '0');
  } catch {
    // Remembering is a convenience; the toggle still works.
  }
  for (const l of listeners) l();
}

/**
 * "Hide numbers" for a shared screen: a class on the root that blurs every
 * marked number (`.num`) and chart (`.num-chart`).
 * Remembered per browser.
 */
export function useHideNumbers(): [boolean, (v: boolean) => void] {
  const value = useSyncExternalStore(
    subscribe,
    () => hidden,
    () => false,
  );
  const set = useCallback((v: boolean) => apply(v), []);
  return [value, set];
}
