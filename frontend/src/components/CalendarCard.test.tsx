// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { CalendarCard } from './CalendarCard';

/** The Google Calendar card: connect through Google's consent, see the link, unlink after a confirm. */

type Data = {
  configured: boolean;
  link: null | { email: string; broken: boolean; created_at: string };
};

function mount(data: Data, search = '') {
  vi.spyOn(api, 'getJson').mockResolvedValue({ data });
  const post = vi
    .spyOn(api, 'postJson')
    .mockResolvedValue({ data: { url: 'https://accounts.google.com/o/oauth2/v2/auth?x=1' } });
  const del = vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
  const go = vi.fn();
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <CalendarCard go={go} search={search} />
    </QueryClientProvider>,
  );
  return { post, del, go };
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('CalendarCard', () => {
  it('connects through the consent URL the server gives', async () => {
    const { post, go } = mount({ configured: true, link: null });
    fireEvent.click(await screen.findByRole('button', { name: 'Connect Google Calendar' }));
    await waitFor(() =>
      expect(go).toHaveBeenCalledWith('https://accounts.google.com/o/oauth2/v2/auth?x=1'),
    );
    expect(post).toHaveBeenCalledWith('/api/v1/calendar/connect');
  });

  it('shows the linked account and unlinks after a confirm', async () => {
    const { del } = mount({
      configured: true,
      link: { email: 'me@example.com', broken: false, created_at: '2026-10-29T10:00:00Z' },
    });
    expect(await screen.findByText(/me@example\.com/)).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Disconnect…' }));
    fireEvent.click(screen.getByRole('button', { name: 'Disconnect' }));
    await waitFor(() => expect(del).toHaveBeenCalledWith('/api/v1/calendar'));
  });

  it('asks to connect again when Google refused the token', async () => {
    mount({
      configured: true,
      link: { email: 'me@example.com', broken: true, created_at: '2026-10-29T10:00:00Z' },
    });
    expect(await screen.findByRole('button', { name: 'Connect again' })).toBeTruthy();
  });

  it('says how the last consent ended', async () => {
    mount({ configured: true, link: null }, '?calendar=denied');
    expect(await screen.findByText(/You did not allow access/)).toBeTruthy();
  });

  it('says when the server has no Google client', async () => {
    mount({ configured: false, link: null });
    expect(await screen.findByText(/no Google OAuth client/)).toBeTruthy();
  });
});
