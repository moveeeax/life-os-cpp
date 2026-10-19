import type { ReactNode } from 'react';

import { LoadError as KitLoadError, SectionFrame, type TabItem } from '@/shell/kit';

export { Placeholder } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/practice', label: 'Deals', end: true },
  { to: '/practice/money', label: 'Money' },
  { to: '/practice/months', label: 'Months' },
  { to: '/practice/clients', label: 'Clients' },
];

/** Frame of every Practice page on the calm sheet. */
export function PracticeFrame({
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
    <SectionFrame eyebrow="Practice" headline={headline} sub={sub} tabs={TABS} tools={actions}>
      {children}
    </SectionFrame>
  );
}

/** A failed query of Practice (a 404 names the module). */
export function LoadError({
  error,
  onRetry,
  notFound,
}: {
  error: unknown;
  onRetry: () => void;
  notFound?: string;
}) {
  return <KitLoadError error={error} onRetry={onRetry} module="practice" notFound={notFound} />;
}

/** The tools row: a spacer, then the section's main button. */
export function Tools({ children }: { children: ReactNode }) {
  return (
    <>
      <span className="k-grow" />
      {children}
    </>
  );
}
