import { describe, expect, it, vi } from 'vitest';

import { ApiClientError } from '@/lib/api/client';

import { batchFailureText, runEach } from './batch';

describe('runEach', () => {
  it('runs the items one after another, in order', async () => {
    const seen: string[] = [];
    let running = 0;
    let overlap = false;
    const outcome = await runEach(['a', 'b', 'c'], async (id) => {
      running += 1;
      if (running > 1) overlap = true;
      await new Promise((r) => setTimeout(r, 1));
      seen.push(id);
      running -= 1;
    });
    expect(seen).toEqual(['a', 'b', 'c']);
    expect(overlap).toBe(false);
    expect(outcome).toEqual({ total: 3, failed: 0, errors: [] });
  });

  it('goes on past a failure and keeps every error', async () => {
    const run = vi.fn(async (id: string) => {
      if (id === 'b') throw new Error('b broke');
    });
    const outcome = await runEach(['a', 'b', 'c'], run);
    expect(run).toHaveBeenCalledTimes(3);
    expect(outcome.total).toBe(3);
    expect(outcome.failed).toBe(1);
    expect((outcome.errors[0] as Error).message).toBe('b broke');
  });

  it('an empty batch is a success', async () => {
    await expect(runEach([], async () => {})).resolves.toEqual({
      total: 0,
      failed: 0,
      errors: [],
    });
  });
});

describe('batchFailureText', () => {
  it('is null when nothing failed', () => {
    expect(batchFailureText({ total: 4, failed: 0, errors: [] })).toBeNull();
  });

  it('counts the failures and quotes the first error', () => {
    const e = new ApiClientError({ status: 409, message: 'already posted', code: 'conflict' });
    expect(batchFailureText({ total: 5, failed: 2, errors: [e, new Error('x')] })).toBe(
      '2 of 5 failed: already posted',
    );
  });
});
