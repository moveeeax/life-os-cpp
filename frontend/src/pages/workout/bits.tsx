import { healthStatusLabel } from '@/lib/workout/session';
import type { WorkoutHealthStatus } from '@/lib/workout/types';
import { cn } from '@/lib/utils';

const BADGE: Record<string, string> = {
  matched: 'k-pos',
  pending: 'k-warn-text',
  no_data: 'k-muted',
};

/** The state of the match between a session and the band data. */
export function HealthBadge({ status }: { status: WorkoutHealthStatus }) {
  return (
    <span className={cn('k-badge', BADGE[status ?? ''] ?? BADGE.no_data)}>
      {healthStatusLabel(status)}
    </span>
  );
}

/**
 * "Label value": one fact of a facts line (put several in a `.k-facts` row).
 * The note shows beside the value as a number too: a tooltip would escape Hide numbers.
 */
export function Stat({ label, value, note }: { label: string; value: string; note?: string }) {
  return (
    <div className="k-stat">
      <span className="k-muted">{label}</span> <span className="num">{value}</span>
      {note && <span className="num k-stat-note"> ({note})</span>}
    </div>
  );
}
