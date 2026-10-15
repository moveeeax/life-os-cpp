import type { KeyboardEvent } from 'react';

/**
 * Enter and Space activate a focusable non-button element (a clickable
 * table row) the way they activate a button. Keys pressed on a control
 * inside the element (a real button in a cell) are left to that control.
 */
export function onActivateKey<T extends HTMLElement>(
  activate: () => void,
): (e: KeyboardEvent<T>) => void {
  return (e) => {
    if (e.target !== e.currentTarget) return;
    if (e.key !== 'Enter' && e.key !== ' ') return;
    e.preventDefault();
    activate();
  };
}
