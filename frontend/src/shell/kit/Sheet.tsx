import { useCallback, useEffect, useId, useRef, type CSSProperties, type ReactNode } from 'react';
import { createPortal } from 'react-dom';

import { useFocusTrap } from '@/hooks/useFocusTrap';

import './kit.css';

interface SheetProps {
  title?: string;
  onClose: () => void;
  children: ReactNode;
  /** Desktop max width in px (default 520). */
  width?: number;
  /** Veto for the implicit closes (scrim, Escape): return false to stay open. */
  confirmClose?: () => boolean;
  role?: 'dialog' | 'alertdialog';
  labelledBy?: string;
  describedBy?: string;
  /** No panel of its own: for content that brings its own (the legacy modals). */
  bare?: boolean;
}

/**
 * Every dialog: over the whole screen through a portal into the shell's
 * #g-portal (inside .glass, so the glass colours and the number blur reach
 * it), centred on a desktop and from the bottom on a phone.
 */
export function Sheet({
  title,
  onClose,
  children,
  width = 520,
  confirmClose,
  role = 'dialog',
  labelledBy,
  describedBy,
  bare,
}: SheetProps) {
  const confirmRef = useRef(confirmClose);
  confirmRef.current = confirmClose;
  const requestClose = useCallback(() => {
    if (confirmRef.current && !confirmRef.current()) return;
    onClose();
  }, [onClose]);
  const ref = useFocusTrap<HTMLDivElement>(requestClose);
  // A dialog without a title or labelledBy takes its name from the first
  // heading of its content (the forms bring their own <h2>).
  const fallbackId = useId();
  useEffect(() => {
    const node = ref.current;
    if (!node || title || labelledBy) return;
    const heading = node.querySelector<HTMLElement>('h1, h2, h3');
    if (!heading) return;
    if (!heading.id) heading.id = fallbackId;
    node.setAttribute('aria-labelledby', heading.id);
  }, [ref, title, labelledBy, fallbackId]);
  // Only a press that started and ended on the scrim dismisses: a text
  // selection dragged out of the panel must not close it.
  const pressedScrim = useRef(false);

  const node = (
    <div
      className="k-scrim"
      onMouseDown={(e) => {
        pressedScrim.current = e.target === e.currentTarget;
      }}
      onClick={(e) => {
        const dismiss = pressedScrim.current && e.target === e.currentTarget;
        pressedScrim.current = false;
        if (dismiss) requestClose();
      }}
    >
      <div
        ref={ref}
        role={role}
        aria-modal="true"
        aria-label={labelledBy ? undefined : title}
        aria-labelledby={labelledBy}
        aria-describedby={describedBy}
        tabIndex={-1}
        className={bare ? 'k-dlg k-bare' : 'k-dlg'}
        style={{ '--k-w': `${width}px` } as CSSProperties}
        onClick={(e) => e.stopPropagation()}
      >
        {!bare && <div className="k-grab" aria-hidden="true" />}
        {title && (
          <div className="k-dlg-head">
            <h2>{title}</h2>
            <button type="button" className="k-x" aria-label="Close" onClick={requestClose}>
              ×
            </button>
          </div>
        )}
        {children}
      </div>
    </div>
  );
  const target =
    typeof document === 'undefined' ? null : (document.getElementById('g-portal') ?? document.body);
  return target ? createPortal(node, target) : node;
}
