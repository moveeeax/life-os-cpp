import type { ReactNode } from 'react';

/** A thin bar: value of max, with an optional label line above. */
export function Meter({
  label,
  value,
  max,
  tone,
}: {
  label?: ReactNode;
  value: number;
  max: number;
  tone?: 'ok' | 'warn' | 'late';
}) {
  const share = max > 0 ? Math.max(0, Math.min(1, value / max)) : 0;
  return (
    <div className="k-meter">
      {label && <div className="k-meter-label">{label}</div>}
      <div
        className="k-meter-bar"
        role="meter"
        aria-valuemin={0}
        aria-valuemax={max}
        aria-valuenow={value}
      >
        <i className={tone ? `k-${tone}` : undefined} style={{ width: `${share * 100}%` }} />
      </div>
    </div>
  );
}
