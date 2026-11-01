import type { ReactNode } from 'react';

import { LoadError as KitLoadError, SectionFrame, type TabItem } from '@/shell/kit';

import './github.css';

export { Placeholder } from '@/shell/kit';

/** Frame of every GitHub page on the calm sheet. */
export function GithubFrame({
  headline,
  sub,
  tabs,
  tools,
  children,
}: {
  headline: ReactNode;
  sub?: ReactNode;
  tabs?: TabItem[];
  tools?: ReactNode;
  children: ReactNode;
}) {
  return (
    <SectionFrame eyebrow="GitHub" headline={headline} sub={sub} tabs={tabs} tools={tools}>
      {children}
    </SectionFrame>
  );
}

/** A failed query of GitHub (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="github" />;
}
