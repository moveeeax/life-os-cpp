import { useId } from 'react';

import { Sheet } from '@/shell/kit';

interface ConfirmDialogProps {
  title: string;
  description?: string;
  confirmLabel?: string;
  destructive?: boolean;
  busy?: boolean;
  onConfirm: () => void;
  onClose: () => void;
}

/**
 * Accessible confirm replacing native confirm(): a small `Sheet`, Escape or
 * the scrim cancels, focus returns on close.
 */
export function ConfirmDialog({
  title,
  description,
  confirmLabel = 'Confirm',
  destructive,
  busy,
  onConfirm,
  onClose,
}: ConfirmDialogProps) {
  const id = useId();
  return (
    <Sheet
      onClose={onClose}
      width={384}
      role="alertdialog"
      labelledBy={`${id}-title`}
      describedBy={description ? `${id}-desc` : undefined}
    >
      <h2 id={`${id}-title`} className="k-confirm-title">
        {title}
      </h2>
      {description && (
        <p id={`${id}-desc`} className="k-confirm-desc">
          {description}
        </p>
      )}
      <div className="k-dlg-actions">
        <button type="button" className="k-btn" onClick={onClose} disabled={busy}>
          Cancel
        </button>
        <button
          type="button"
          className={destructive ? 'k-btn k-danger' : 'k-btn k-solid'}
          onClick={onConfirm}
          disabled={busy}
        >
          {busy ? 'Working…' : confirmLabel}
        </button>
      </div>
    </Sheet>
  );
}
