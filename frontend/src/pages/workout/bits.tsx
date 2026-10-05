import { healthStatusLabel } from '@/lib/workout/session';
import type { WorkoutHealthStatus } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

const BADGE: Record<string, string> = {
  matched: 'bg-success-50 text-success-600 dark:bg-success-500/15 dark:text-success-500',
  pending: 'bg-warning-50 text-warning-600 dark:bg-warning-500/15 dark:text-orange-400',
  no_data: 'bg-gray-100 text-gray-600 dark:bg-white/5 dark:text-gray-400',
};

/** The state of the match between a session and the band data. */
export function HealthBadge({ status }: { status: WorkoutHealthStatus }) {
  return (
    <span
      className={cn(
        'inline-flex rounded-full px-2.5 py-0.5 text-theme-xs font-medium whitespace-nowrap',
        BADGE[status ?? ''] ?? BADGE.no_data,
      )}
    >
      {healthStatusLabel(status)}
    </span>
  );
}

/** A label over a value; the tiles of the session pages. */
export function Stat({ label, value, note }: { label: string; value: string; note?: string }) {
  return (
    <div>
      <p className="text-theme-xs text-gray-500 dark:text-gray-400">{label}</p>
      <p className="mt-0.5 text-lg font-semibold text-gray-800 tabular-nums dark:text-white/90">
        {value}
      </p>
      {note && <p className="text-theme-xs text-gray-500 dark:text-gray-400">{note}</p>}
    </div>
  );
}
