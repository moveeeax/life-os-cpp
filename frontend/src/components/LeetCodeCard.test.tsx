// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { LeetCodeCard } from './LeetCodeCard';

/** The LeetCode card on /account: the username, the last pull and its counts. */

let profile: Record<string, unknown> | null = null;

beforeEach(() => {
  profile = null;
  vi.spyOn(api, 'getJson').mockImplementation(async () => ({ data: profile }));
  vi.spyOn(api, 'putJson').mockImplementation(async (_p: string, opts?: { body?: unknown }) => {
    profile = {
      ...(opts?.body as object),
      synced_at: null,
      last_error: null,
      total: null,
      easy: null,
      medium: null,
      hard: null,
    };
    return { data: profile };
  });
  vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function show() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <LeetCodeCard />
    </QueryClientProvider>,
  );
}

describe('LeetCodeCard', () => {
  it('sets the username with the local zone', async () => {
    show();
    fireEvent.change(await screen.findByLabelText('LeetCode username'), {
      target: { value: 'moveeeax' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    await waitFor(() =>
      expect(api.putJson).toHaveBeenCalledWith('/api/v1/leetcode/profile', {
        body: { username: 'moveeeax', tz: expect.any(String) },
      }),
    );
  });

  it('shows the counts of the last pull, marked for Hide numbers', async () => {
    profile = {
      username: 'moveeeax',
      tz: 'Asia/Almaty',
      synced_at: '2026-10-29T06:00:00Z',
      last_error: null,
      total: 807,
      easy: 208,
      medium: 451,
      hard: 148,
    };
    show();
    const line = await screen.findByText(/807 solved/);
    expect(line.closest('.num')).toBeTruthy();
  });

  it('says why the last pull failed', async () => {
    profile = {
      username: 'moveeeax',
      tz: 'UTC',
      synced_at: null,
      last_error: 'no such public profile',
      total: null,
      easy: null,
      medium: null,
      hard: null,
    };
    show();
    expect(await screen.findByText(/no such public profile/)).toBeTruthy();
  });
});
