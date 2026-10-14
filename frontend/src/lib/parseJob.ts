// The rules of a parse job (food, money, tasks): what one poll answer means,
// and the words of a failed start or poll. The hook is hooks/useParseJob.ts.
import { ApiClientError, apiErrorMessage } from '@/lib/api/client';

export type ParseState = 'idle' | 'starting' | 'running' | 'done' | 'failed';

export interface JobLike {
  id: string;
  status: string;
  error?: string | null;
}

/** The end a poll answer reached, or null to keep polling. */
export function afterPoll(
  job: Pick<JobLike, 'status' | 'error'>,
): { state: 'done' } | { state: 'failed'; error: string | null } | null {
  if (job.status === 'done') return { state: 'done' };
  if (job.status === 'failed') return { state: 'failed', error: job.error ?? null };
  return null;
}

/** A start that failed: @p notConfigured when the reader is not set up, else the server's words. */
export function enqueueErrorText(e: unknown, notConfigured: string): string {
  return e instanceof ApiClientError && e.code === 'not_configured'
    ? notConfigured
    : apiErrorMessage(e);
}

/** A poll that failed: the words that end it (404), or null to keep asking. */
export function pollErrorText(e: unknown): string | null {
  return e instanceof ApiClientError && e.status === 404 ? 'The parse job is gone.' : null;
}
