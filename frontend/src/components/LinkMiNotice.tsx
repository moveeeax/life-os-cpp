import { Link } from 'react-router';
import { Watch } from 'lucide-react';

interface LinkMiNoticeProps {
  /** What the user gets by linking, e.g. "to see your steps, sleep and heart rate". */
  purpose: string;
  /** Tighter variant for use inside another card. */
  compact?: boolean;
}

/**
 * Shown on dashboard pages to a user who has not linked a Mi account yet:
 * there is no band data to show, and the profile is where linking happens.
 */
export function LinkMiNotice({ purpose, compact }: LinkMiNoticeProps) {
  return (
    <div
      className={
        compact
          ? 'flex flex-wrap items-center gap-3'
          : 'flex flex-wrap items-center gap-4 rounded-2xl border border-gray-200 bg-white p-5 sm:p-6 dark:border-gray-800 dark:bg-white/3'
      }
    >
      <Watch className="size-6 shrink-0 text-brand-500" aria-hidden="true" />
      <p className="min-w-0 flex-1 text-theme-sm text-gray-700 dark:text-gray-300">
        Link your Mi account {purpose}.
      </p>
      <Link
        to="/account"
        className="inline-flex items-center justify-center rounded-lg bg-brand-500 px-4 py-2.5 text-sm font-medium text-white shadow-theme-xs transition hover:bg-brand-600"
      >
        Link in profile
      </Link>
    </div>
  );
}
