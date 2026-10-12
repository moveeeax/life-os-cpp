import { useCallback, useEffect, useState } from 'react';

const KEY = 'hide-numbers';
const CLASS = 'hide-numbers';

function stored(): boolean {
  try {
    return globalThis.localStorage?.getItem(KEY) === '1';
  } catch {
    return false;
  }
}

/**
 * "Hide numbers" for a shared screen: a class on the root that blurs every
 * marked number (and the whole sheet of a section not redesigned yet).
 * Remembered per browser.
 */
export function useHideNumbers(): [boolean, (v: boolean) => void] {
  const [hidden, setHidden] = useState(stored);

  useEffect(() => {
    document.documentElement.classList.toggle(CLASS, hidden);
  }, [hidden]);

  const set = useCallback((v: boolean) => {
    setHidden(v);
    try {
      globalThis.localStorage?.setItem(KEY, v ? '1' : '0');
    } catch {
      // Remembering is a convenience; the toggle still works.
    }
  }, []);

  return [hidden, set];
}
