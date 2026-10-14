import type { ReactNode } from 'react';

import { LoadError as KitLoadError, SectionFrame, type TabItem } from '@/shell/kit';

export { Placeholder } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/travel', label: 'Trips', end: true },
  { to: '/travel/days', label: 'Days' },
  { to: '/travel/rules', label: 'Rules' },
  { to: '/travel/documents', label: 'Documents' },
];

/** Frame of every Travel page on the calm sheet. */
export function TravelFrame({
  title,
  headline,
  sub,
  actions,
  children,
}: {
  title: string;
  headline?: ReactNode;
  sub?: ReactNode;
  actions?: ReactNode;
  children: ReactNode;
}) {
  return (
    <SectionFrame
      eyebrow="Travel"
      headline={headline ?? title}
      sub={sub}
      tabs={TABS}
      tools={actions}
    >
      {children}
    </SectionFrame>
  );
}

/** A failed query of Travel (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="travel" />;
}
