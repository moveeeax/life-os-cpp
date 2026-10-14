import type { ReactNode } from 'react';
import { Group, LoadError as KitLoadError, SectionFrame, type TabItem } from '@/shell/kit';

export { Placeholder } from '@/shell/kit';

/** A failed query of Tasks (a 404 names the module). */
export function LoadError({ error, onRetry }: { error: unknown; onRetry: () => void }) {
  return <KitLoadError error={error} onRetry={onRetry} module="tasks" />;
}

interface TasksFrameProps {
  /** Quiet counts beside the tab names; zero shows nothing. */
  counts?: { today?: number; later?: number; inbox?: number };
  /** In place of the date (the Projects tab says how many are active). */
  headline?: ReactNode;
  sub?: ReactNode;
  tools?: ReactNode;
  children: ReactNode;
}

/** One narrow column on the calm sheet: the local date, the tabs, the page. */
export function TasksFrame({ counts, headline, sub, tools, children }: TasksFrameProps) {
  const date = new Date().toLocaleDateString('en-GB', {
    weekday: 'long',
    day: 'numeric',
    month: 'long',
  });
  const tabs: TabItem[] = [
    { to: '/tasks', label: 'Today', count: counts?.today, end: true },
    { to: '/tasks/later', label: 'Later', count: counts?.later },
    { to: '/tasks/inbox', label: 'Inbox', count: counts?.inbox },
    { to: '/tasks/projects', label: 'Projects' },
  ];
  return (
    <SectionFrame eyebrow="Tasks" headline={headline ?? date} sub={sub} tabs={tabs} tools={tools}>
      {children}
    </SectionFrame>
  );
}

/** A section caption: small, grey, upper case; the late one in red. */
export function Section({
  title,
  tone,
  children,
}: {
  title: string;
  tone?: 'late';
  children: ReactNode;
}) {
  return (
    <Group label={title} tone={tone}>
      {children}
    </Group>
  );
}
