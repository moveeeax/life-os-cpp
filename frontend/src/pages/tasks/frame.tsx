import type { ReactNode } from 'react';
import { NavLink } from 'react-router';

import { cn } from '@/lib/utils';

export { LoadError, Placeholder } from '../money/frame';

interface TasksFrameProps {
  /** Quiet counts beside the tab names; zero shows nothing. */
  counts?: { today?: number; later?: number; inbox?: number };
  children: ReactNode;
}

const TABS = [
  { to: '/tasks', label: 'Today', key: 'today', end: true },
  { to: '/tasks/later', label: 'Later', key: 'later', end: false },
  { to: '/tasks/inbox', label: 'Inbox', key: 'inbox', end: false },
] as const;

/** One narrow column: the title with the local date, three tabs, the page. */
export function TasksFrame({ counts, children }: TasksFrameProps) {
  const date = new Date().toLocaleDateString('en-GB', {
    weekday: 'long',
    day: 'numeric',
    month: 'long',
  });
  return (
    <div className="mx-auto grid max-w-2xl gap-7">
      <div className="flex flex-wrap items-baseline justify-between gap-3">
        <h1 className="text-title-sm font-semibold text-gray-800 dark:text-white/90">Tasks</h1>
        <span className="text-theme-sm text-gray-500">{date}</span>
      </div>
      <nav
        aria-label="Task views"
        className="flex gap-5 border-b border-gray-200 dark:border-white/10"
      >
        {TABS.map((tab) => {
          const n = counts?.[tab.key] ?? 0;
          return (
            <NavLink
              key={tab.to}
              to={tab.to}
              end={tab.end}
              className={({ isActive }) =>
                cn(
                  '-mb-px border-b-2 pb-2.5 text-theme-sm font-medium',
                  isActive
                    ? 'border-gray-800 text-gray-800 dark:border-white/90 dark:text-white/90'
                    : 'border-transparent text-gray-500 hover:text-gray-700 dark:hover:text-gray-300',
                )
              }
            >
              {tab.label}
              {n > 0 && <span className="ms-1 font-normal text-gray-400">{n}</span>}
            </NavLink>
          );
        })}
      </nav>
      {children}
    </div>
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
    <section aria-label={title}>
      <h2
        className={cn(
          'mb-1 text-theme-xs font-medium tracking-wider uppercase',
          tone === 'late' ? 'text-error-500' : 'text-gray-400',
        )}
      >
        {title}
      </h2>
      {children}
    </section>
  );
}
