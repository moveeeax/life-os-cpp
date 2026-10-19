import type { ReactNode } from 'react';

import { LoadError as KitLoadError, SectionFrame } from '@/shell/kit';

import { BODY_TABS } from './tabs';

import './medical.css';

export { Placeholder } from '@/shell/kit';

/** Frame of every page of the medical archive. */
export function MedicalFrame({
  headline,
  sub,
  actions,
  children,
}: {
  headline: ReactNode;
  sub?: ReactNode;
  actions?: ReactNode;
  children: ReactNode;
}) {
  return (
    <SectionFrame eyebrow="Body" headline={headline} sub={sub} tabs={BODY_TABS} tools={actions}>
      {children}
    </SectionFrame>
  );
}

/** A failed query of the archive (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="medical" />;
}

/** The "New ..." button of a tools row. */
export function NewButton({ label, onClick }: { label: string; onClick: () => void }) {
  return (
    <>
      <span className="k-grow" />
      <button type="button" className="k-btn k-solid" onClick={onClick}>
        {label}
      </button>
    </>
  );
}
