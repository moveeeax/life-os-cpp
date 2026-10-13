import { ApiClientError, apiErrorMessage } from '@/lib/api/client';
import { cn } from '@/lib/utils';

/** A failed query: the server's message, or a plain one when the section's module is off. */
export function LoadError({
  error,
  onRetry,
  module,
}: {
  error: unknown;
  onRetry: () => void;
  /** The module a 404 points at, e.g. "money". */
  module: string;
}) {
  const off = error instanceof ApiClientError && error.status === 404 && error.code === 'not_found';
  return (
    <div role="alert" className="k-banner">
      <span className="k-error-text">
        {off
          ? `Not found. The ${module} module may be off on this server.`
          : apiErrorMessage(error, 'Failed to load.')}
      </span>
      <button type="button" onClick={onRetry}>
        Retry →
      </button>
    </div>
  );
}

/** A grey block while a query loads. */
export function Placeholder({ className }: { className?: string }) {
  return <div aria-hidden="true" className={cn('k-placeholder', className)} />;
}
