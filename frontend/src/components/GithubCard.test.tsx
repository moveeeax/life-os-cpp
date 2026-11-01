// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ApiClientError, api } from '@/lib/api/client';
import type { GithubStatus } from '@/lib/github';

import { GithubCard } from './GithubCard';

/** The GitHub card: connect through GitHub's page, see the link and the last sync, sync now, unlink after a confirm. */

const NOW = new Date('2026-11-01T12:00:00Z');

const linked: GithubStatus = {
  configured: true,
  linked: true,
  login: 'moveeeax',
  scopes: ['repo', 'read:org'],
  linked_at: '2026-11-01T08:00:00Z',
  synced_at: '2026-11-01T09:00:00Z',
  sync_error: null,
  org_hint: null,
};

const unlinked: GithubStatus = {
  ...linked,
  linked: false,
  login: null,
  scopes: [],
  linked_at: null,
  synced_at: null,
};

function mount(data: GithubStatus, search = '') {
  vi.spyOn(api, 'getJson').mockResolvedValue({ data });
  const post = vi.spyOn(api, 'postJson').mockResolvedValue(undefined);
  const del = vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
  const go = vi.fn();
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <GithubCard go={go} search={search} now={() => NOW} />
      </MemoryRouter>
    </QueryClientProvider>,
  );
  return { post, del, go };
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('GithubCard', () => {
  it('says when the server has no OAuth App', async () => {
    mount({ ...unlinked, configured: false });
    expect(await screen.findByText(/no GitHub OAuth App/)).toBeTruthy();
    expect(screen.queryByRole('button')).toBeNull();
  });

  it('connects by going to the oauth start route', async () => {
    const { go } = mount(unlinked);
    fireEvent.click(await screen.findByRole('button', { name: 'Connect GitHub' }));
    expect(go).toHaveBeenCalledWith('/api/v1/github/oauth/start');
  });

  it('shows the login and the age of the last sync', async () => {
    mount(linked);
    expect(await screen.findByText(/Connected as moveeeax/)).toBeTruthy();
    expect(screen.getByText(/synced 3 h ago/)).toBeTruthy();
  });

  it('says the sync error in words', async () => {
    mount({ ...linked, sync_error: 'GitHub answered 401' });
    expect(await screen.findByText(/The last sync failed: GitHub answered 401/)).toBeTruthy();
  });

  it('tells which organisation to approve the app in', async () => {
    mount({ ...linked, org_hint: 'cybercapybara' });
    expect(await screen.findByText(/cybercapybara may restrict third-party apps/)).toBeTruthy();
  });

  it('queues a sync, and sleeps for a minute after a 429', async () => {
    const { post } = mount(linked);
    fireEvent.click(await screen.findByRole('button', { name: 'Sync now' }));
    await waitFor(() => expect(post).toHaveBeenCalledWith('/api/v1/github/sync'));
    expect(await screen.findByText(/sync queued/)).toBeTruthy();
    post.mockRejectedValueOnce(
      new ApiClientError({
        status: 429,
        message: 'too soon',
        code: 'sync_too_soon',
        body: { error: 'sync_too_soon', retry_after_sec: 40 },
      }),
    );
    fireEvent.click(screen.getByRole('button', { name: 'Sync now' }));
    const asleep = await screen.findByRole('button', { name: 'Synced a moment ago' });
    expect((asleep as HTMLButtonElement).disabled).toBe(true);
    expect(screen.queryByText(/too soon/)).toBeNull();
  });

  it('unlinks after a confirm', async () => {
    const { del } = mount(linked);
    fireEvent.click(await screen.findByRole('button', { name: 'Disconnect…' }));
    fireEvent.click(screen.getByRole('button', { name: 'Disconnect' }));
    await waitFor(() => expect(del).toHaveBeenCalledWith('/api/v1/github'));
  });

  it('says how the authorization ended', async () => {
    mount(unlinked, '?github=denied');
    expect(await screen.findByText(/You did not allow access/)).toBeTruthy();
  });
});
