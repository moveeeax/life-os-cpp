import { useMetrics } from '@/hooks/useMetrics';
import { entryOf, groupMetrics, optionText } from '@/lib/metrics';

// The value of the "Manual" option; null outside.
const MANUAL = '';

/**
 * One series of the metrics catalog as a native select, grouped by section.
 * null is "Manual" when @p manual names that option; an entry the caller
 * cannot read is listed disabled with the reason, unless it is the current one.
 */
export function MetricPicker({
  id,
  value,
  onChange,
  manual,
  exclude,
  className,
}: {
  id?: string;
  value: string | null;
  onChange: (source: string | null) => void;
  /** The label of the first option that stands for no series; none offers no such option. */
  manual?: string;
  /** Ids left out (the habit being edited). */
  exclude?: readonly string[];
  className?: string;
}) {
  const metrics = useMetrics();
  const entries = metrics.data ?? [];
  const groups = groupMetrics(entries, exclude);
  // A stored id the catalog no longer lists (or has not loaded yet) stays selectable.
  const stray = value !== null && !entryOf(entries, value) ? value : null;
  return (
    <select
      id={id}
      className={className}
      value={value ?? MANUAL}
      onChange={(e) => onChange(e.target.value === MANUAL ? null : e.target.value)}
    >
      {manual !== undefined && <option value={MANUAL}>{manual}</option>}
      {stray && <option value={stray}>{metrics.isPending ? 'Loading…' : stray}</option>}
      {groups.map((g) => (
        <optgroup key={g.key} label={g.label}>
          {g.entries.map((e) => (
            <option key={e.id} value={e.id} disabled={!e.available && e.id !== value}>
              {optionText(e)}
            </option>
          ))}
        </optgroup>
      ))}
    </select>
  );
}
