import type { ReactNode } from 'react';

import { Sheet, dialogWidth } from '@/shell/kit';

interface ModalProps {
  onClose: () => void;
  children: ReactNode;
  /** Width class for the panel (default max-w-2xl). */
  className?: string;
  /**
   * Optional veto for the *implicit* closes (backdrop click, Escape). Return
   * false to keep the modal open — e.g. a form with unsaved changes that asks
   * for confirmation first. Omitted (the default) means always close.
   */
  confirmClose?: () => boolean;
}

/**
 * A large form or content over the whole screen: a bare `Sheet` (the content
 * brings its own panel), centred on a desktop and from the bottom on a phone.
 * Backdrop click and Escape close it (subject to `confirmClose`); focus is
 * restored on close.
 */
export function Modal({ onClose, children, className, confirmClose }: ModalProps) {
  return (
    <Sheet onClose={onClose} width={dialogWidth(className)} confirmClose={confirmClose} bare>
      {children}
    </Sheet>
  );
}
