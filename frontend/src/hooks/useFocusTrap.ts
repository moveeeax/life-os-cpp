import { useEffect, useRef, useState } from 'react';

// Open traps, outermost first: a confirm over a modal must not close both on Escape.
const stack: object[] = [];

const FOCUSABLE =
  'a[href],button:not([disabled]),textarea:not([disabled]),input:not([disabled]),select:not([disabled]),[tabindex]:not([tabindex="-1"])';

const FIELD =
  'input:not([disabled]):not([type="hidden"]),textarea:not([disabled]),select:not([disabled])';

/**
 * Where the focus goes when a dialog opens: a field that took it itself
 * (autoFocus) keeps it; else the first field; else the first control that is
 * not the header's Close ×; else the dialog. On a touch screen a field is
 * skipped for the dialog itself, so opening a sheet does not pop the keyboard.
 */
export function initialFocus(node: HTMLElement, coarse: boolean): HTMLElement {
  const active = document.activeElement as HTMLElement | null;
  if (active && active !== node && node.contains(active)) return active;
  const field = node.querySelector<HTMLElement>(FIELD);
  if (field) return coarse ? node : field;
  const controls = Array.from(node.querySelectorAll<HTMLElement>(FOCUSABLE));
  return controls.find((c) => !c.classList.contains('k-x')) ?? controls[0] ?? node;
}

const coarsePointer = () =>
  typeof window.matchMedia === 'function' && window.matchMedia('(pointer: coarse)').matches;

/**
 * Modal a11y in one hook: traps Tab within the returned ref'd container, closes
 * on Escape, focuses the first field (initialFocus) on open, and restores focus
 * to the previously-focused element on unmount. Put the ref on the dialog element.
 */
export function useFocusTrap<T extends HTMLElement>(onClose: () => void) {
  const ref = useRef<T>(null);
  // The latest close callback, read on Escape: a parent re-render passes a new
  // function, and re-running the effect would steal focus from the field being
  // typed in and reorder the stack of open traps.
  const close = useRef(onClose);
  close.current = onClose;
  // The element focused before the dialog rendered (read on the first render:
  // by the effect, an autoFocus field inside may already hold the focus).
  const [opener] = useState(() => document.activeElement as HTMLElement | null);
  useEffect(() => {
    const node = ref.current;
    const previouslyFocused = opener;
    const focusables = () => Array.from(node?.querySelectorAll<HTMLElement>(FOCUSABLE) ?? []);
    if (node) initialFocus(node, coarsePointer()).focus();

    const me = {};
    stack.push(me);

    const onKey = (e: KeyboardEvent) => {
      if (stack[stack.length - 1] !== me) return;
      if (e.key === 'Escape') {
        e.preventDefault();
        close.current();
        return;
      }
      if (e.key !== 'Tab') return;
      const items = focusables();
      if (items.length === 0) return;
      const first = items[0];
      const last = items[items.length - 1];
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    };
    document.addEventListener('keydown', onKey);
    return () => {
      stack.splice(stack.indexOf(me), 1);
      document.removeEventListener('keydown', onKey);
      previouslyFocused?.focus?.();
    };
  }, [opener]);

  return ref;
}
