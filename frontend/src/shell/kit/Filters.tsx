import { useState, type ReactNode } from 'react';

import { Sheet } from './Sheet';

/** "Filter · N": the section's selects in a sheet instead of a row over the list. */
export function Filters({
  active,
  onClear,
  children,
  resultLabel = 'Show',
}: {
  active: number;
  onClear: () => void;
  children: ReactNode;
  resultLabel?: string;
}) {
  const [open, setOpen] = useState(false);
  return (
    <>
      <button type="button" className="k-btn" aria-haspopup="dialog" onClick={() => setOpen(true)}>
        Filter{active > 0 ? ` · ${active}` : ''}
      </button>
      {open && (
        <Sheet title="Filter" onClose={() => setOpen(false)} width={460}>
          <div className="k-form">{children}</div>
          <div className="k-dlg-actions">
            <button type="button" className="k-btn" onClick={onClear}>
              Clear
            </button>
            <button type="button" className="k-btn k-solid" onClick={() => setOpen(false)}>
              {resultLabel}
            </button>
          </div>
        </Sheet>
      )}
    </>
  );
}
