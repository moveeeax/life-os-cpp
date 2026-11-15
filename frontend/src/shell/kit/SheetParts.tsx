import type { ReactNode } from 'react';

/** A sheet form's footer: Cancel, and the submit button that reads Saving… while busy. */
export function SheetActions({
  busy,
  label,
  onCancel,
}: {
  busy: boolean;
  label: string;
  onCancel: () => void;
}) {
  return (
    <div className="k-dlg-actions">
      <button type="button" className="k-btn" onClick={onCancel}>
        Cancel
      </button>
      <button type="submit" className="k-btn k-solid" disabled={busy}>
        {busy ? 'Saving…' : label}
      </button>
    </div>
  );
}

/** The error line above a sheet's footer; nothing when there is no error. */
export function SheetError({ error }: { error: string | null }) {
  return error ? (
    <p role="alert" className="k-error">
      {error}
    </p>
  ) : null;
}

/** A sheet's `.k-form` that submits without a page load. */
export function SheetForm({ onSubmit, children }: { onSubmit: () => void; children: ReactNode }) {
  return (
    <form
      className="k-form"
      onSubmit={(e) => {
        e.preventDefault();
        onSubmit();
      }}
    >
      {children}
    </form>
  );
}
