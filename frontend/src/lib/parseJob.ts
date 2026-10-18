// The rules of a parse job (food, money, tasks): what one poll answer means,
// and the words of a failed start or poll. The hook is hooks/useParseJob.ts.
import { ApiClientError, apiErrorMessage } from '@/lib/api/client';

export type ParseState = 'idle' | 'starting' | 'running' | 'done' | 'failed';

export interface JobLike {
  id: string;
  status: string;
  error?: string | null;
}

/**
 * A readable sentence for a failed list parse's `error` ("<code>: <detail>"),
 * shared by food and money; @p words names what differs per module.
 */
export function jobErrorText(
  error: string | null | undefined,
  words: { notConfigured: string; disabledCode: string; disabled: string },
): string {
  if (!error) return 'The parse failed.';
  const colon = error.indexOf(':');
  const code = colon === -1 ? error : error.slice(0, colon);
  const detail = colon === -1 ? '' : error.slice(colon + 1).trim();
  if (code === 'invalid_answer')
    return `The model did not answer with a usable list${detail ? `: ${detail}` : '.'}`;
  if (code === 'not_configured') return words.notConfigured;
  if (code === words.disabledCode) return words.disabled;
  if (code === 'provider_refused') return 'The language model provider refused the request.';
  if (code === 'provider_unavailable')
    return 'The language model provider did not answer after several tries. Try again later.';
  if (code === 'parse_timeout') return 'The parse is taking too long. Try again in a minute.';
  const status = /^provider_error_(\d+)$/.exec(code);
  if (status)
    return `The language model provider answered with an error (${status[1]})${detail ? `: ${detail}` : '.'}`;
  return detail || code;
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
