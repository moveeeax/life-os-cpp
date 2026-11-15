import { useCallback, useState, type ReactNode } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';

/** The part of a delete mutation the confirm needs. */
interface Remove<K> {
  mutate: (id: K, options?: { onSuccess?: () => void }) => void;
  isPending: boolean;
}

/** What the confirm says about the row; the button reads Delete unless told otherwise. */
interface Words {
  title: string;
  description: string;
  confirmLabel?: string;
}

/**
 * The row a list is about to delete and its confirm dialog. `ask(row)` opens
 * it, `close` drops it (hand it to the delete hook's onSuccess so the dialog
 * closes as soon as the row is gone), and `dialog(remove, words)` renders the
 * destructive ConfirmDialog for the open row, or nothing.
 *
 * A delete hook without an onSuccess of its own passes `closeOnSuccess`: the
 * dialog then closes from the mutate call.
 */
export function useConfirmDelete<T extends { id: K }, K = string>() {
  const [row, setRow] = useState<T | null>(null);
  const close = useCallback(() => setRow(null), []);
  const dialog = (
    remove: Remove<K>,
    words: (row: T) => Words,
    { closeOnSuccess = false }: { closeOnSuccess?: boolean } = {},
  ): ReactNode => {
    if (!row) return null;
    const { title, description, confirmLabel = 'Delete' } = words(row);
    return (
      <ConfirmDialog
        title={title}
        description={description}
        confirmLabel={confirmLabel}
        destructive
        busy={remove.isPending}
        onConfirm={() =>
          closeOnSuccess ? remove.mutate(row.id, { onSuccess: close }) : remove.mutate(row.id)
        }
        onClose={close}
      />
    );
  };
  return { row, ask: setRow, close, dialog };
}
