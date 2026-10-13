import type { ReactNode } from 'react';
import { Link } from 'react-router';

/** One muted line with an optional link or button on the right. */
export function Banner({
  children,
  to,
  action,
  onAction,
}: {
  children: ReactNode;
  to?: string;
  action?: string;
  onAction?: () => void;
}) {
  return (
    <div className="k-banner">
      <span>{children}</span>
      {action && to && <Link to={to}>{action} →</Link>}
      {action && !to && onAction && (
        <button type="button" onClick={onAction}>
          {action} →
        </button>
      )}
    </div>
  );
}
