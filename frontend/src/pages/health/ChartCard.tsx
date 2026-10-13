import type { ReactNode } from 'react';

import { apiErrorMessage } from '@/lib/api/client';
import { Group, Row } from '@/shell/kit';

interface ChartCardProps {
  title: string;
  /** One line under the title, e.g. what the series are. */
  hint?: string;
  isPending: boolean;
  error: unknown;
  onRetry: () => void;
  /** True when the query succeeded but there is nothing to draw. */
  isEmpty: boolean;
  emptyText?: string;
  /** Given: a row whose details hold the chart, mounted only while open. */
  open?: boolean;
  onToggle?: (open: boolean) => void;
  /** The value on the right of a closed row. */
  right?: ReactNode;
  children: ReactNode;
}

function Body({
  isPending,
  error,
  onRetry,
  isEmpty,
  emptyText,
  children,
}: Pick<ChartCardProps, 'isPending' | 'error' | 'onRetry' | 'isEmpty' | 'emptyText' | 'children'>) {
  if (isPending) return <div className="k-placeholder k-chart-ph" aria-label="Loading" />;
  if (error)
    return (
      <div role="alert" className="k-banner">
        <span className="k-error-text">{apiErrorMessage(error, 'Failed to load.')}</span>
        <button type="button" onClick={onRetry}>
          Retry →
        </button>
      </div>
    );
  if (isEmpty) return <p className="k-empty">{emptyText}</p>;
  return <>{children}</>;
}

/**
 * One Health block on the calm sheet: a group that is always open, or (with
 * onToggle) a row whose chart opens on a click. Each block loads on its own: a
 * failed query shows its error and a retry here and leaves the others alone.
 */
export function ChartCard({
  title,
  hint,
  open,
  onToggle,
  right,
  emptyText = 'No data in this period.',
  ...rest
}: ChartCardProps) {
  if (onToggle) {
    return (
      <Row title={title} sub={hint} right={right} open={open} onToggle={onToggle}>
        {/* Row mounts its details only while open, so the chart loads its canvas on demand. */}
        <Body emptyText={emptyText} {...rest} />
      </Row>
    );
  }
  return (
    <Group label={title}>
      {hint && <p className="k-sub">{hint}</p>}
      <Body emptyText={emptyText} {...rest} />
    </Group>
  );
}
