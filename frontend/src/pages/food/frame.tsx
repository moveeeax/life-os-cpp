import type { ReactNode } from 'react';

import { SectionFrame, type TabItem } from '@/shell/kit';
import { LoadError as KitLoadError } from '@/shell/kit';

const TABS: TabItem[] = [
  { to: '/food', label: 'Day', end: true },
  { to: '/food/week', label: 'Week' },
  { to: '/food/items', label: 'Products' },
  { to: '/food/goals', label: 'Goals' },
];

interface FoodFrameProps {
  /** The page's own name; the headline when the page gives none. */
  title: string;
  headline?: ReactNode;
  sub?: ReactNode;
  /** The tools line: date, the main action. */
  actions?: ReactNode;
  children: ReactNode;
}

/**
 * Frame of every Food page on the calm sheet. Any confirmed user may keep a
 * diary, so there is no permission check here; a switched-off module answers
 * 404 and LoadError says so.
 */
export function FoodFrame({ title, headline, sub, actions, children }: FoodFrameProps) {
  return (
    <SectionFrame eyebrow="Food" headline={headline ?? title} sub={sub} tabs={TABS} tools={actions}>
      {children}
    </SectionFrame>
  );
}

/** A failed query of this section (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="food" />;
}

export { Placeholder } from '@/shell/kit';
