import type { ReactNode } from 'react';

import { LoadError as KitLoadError, SectionFrame } from '@/shell/kit';

export { Placeholder } from '@/shell/kit';

/** Frame of every Notes page on the calm sheet. */
export function NotesFrame({
  headline,
  sub,
  tools,
  children,
}: {
  headline: ReactNode;
  sub?: ReactNode;
  tools?: ReactNode;
  children: ReactNode;
}) {
  return (
    <SectionFrame eyebrow="Notes" headline={headline} sub={sub} tools={tools}>
      {children}
    </SectionFrame>
  );
}

/** A failed query of Notes (a 404 names the module). */
export function LoadError({
  error,
  onRetry,
  notFound,
}: {
  error: unknown;
  onRetry: () => void;
  notFound?: string;
}) {
  return <KitLoadError error={error} onRetry={onRetry} module="notes" notFound={notFound} />;
}
