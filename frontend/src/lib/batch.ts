import { apiErrorMessage } from '@/lib/api/client';

/** What became of a batch: how many items there were, how many failed, and why. */
export interface BatchOutcome {
  total: number;
  failed: number;
  errors: unknown[];
}

/**
 * Runs @p run on every item one after another, never in parallel, and goes
 * on past a failure: "Post all" posts what it can. Nothing is thrown; the
 * outcome says what failed.
 */
export async function runEach<T>(
  items: readonly T[],
  run: (item: T) => Promise<unknown>,
): Promise<BatchOutcome> {
  const errors: unknown[] = [];
  for (const item of items) {
    try {
      await run(item);
    } catch (e) {
      errors.push(e);
    }
  }
  return { total: items.length, failed: errors.length, errors };
}

/** "2 of 5 failed: <the first error>", or null when every item went through. */
export function batchFailureText(outcome: BatchOutcome): string | null {
  if (outcome.failed === 0) return null;
  return `${outcome.failed} of ${outcome.total} failed: ${apiErrorMessage(outcome.errors[0])}`;
}
