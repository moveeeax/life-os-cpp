import type { ReactNode } from 'react';
import { Group, SectionFrame, type TabItem } from '@/shell/kit';

export { LoadError, Placeholder } from '../money/frame';

interface TasksFrameProps {
  /** Quiet counts beside the tab names; zero shows nothing. */
  counts?: { today?: number; later?: number; inbox?: number };
  children: ReactNode;
}

/** One narrow column on the calm sheet: the local date, three tabs, the page. */
export function TasksFrame({ counts, children }: TasksFrameProps) {
  const date = new Date().toLocaleDateString('en-GB', {
    weekday: 'long',
    day: 'numeric',
    month: 'long',
  });
  const tabs: TabItem[] = [
    { to: '/tasks', label: 'Today', count: counts?.today, end: true },
    { to: '/tasks/later', label: 'Later', count: counts?.later },
    { to: '/tasks/inbox', label: 'Inbox', count: counts?.inbox },
  ];
  return (
    <SectionFrame eyebrow="Tasks" headline={date} tabs={tabs}>
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
