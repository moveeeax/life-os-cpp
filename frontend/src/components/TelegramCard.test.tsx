// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { TelegramCard } from './TelegramCard';

/** The Telegram card: link when the server has a token, unlink after a second press. */

type Data = {
  configured: boolean;
  linked: boolean;
  elsewhere: boolean;
  link: null | {
    me_id: string;
    username: string;
    polled_at: string | null;
    last_error: string | null;
    created_at: string;
  };
};

function mount(data: Data) {
  vi.spyOn(api, 'getJson').mockResolvedValue({ data });
  vi.spyOn(api, 'putJson').mockResolvedValue({ data: {} });
  vi.spyOn(api, 'deleteJson').mockResolvedValue(undefined);
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <TelegramCard />
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('TelegramCard', () => {
  it('says the token is missing', async () => {
    mount({ configured: false, linked: false, elsewhere: false, link: null });
    expect(await screen.findByText(/no gateway token/)).toBeTruthy();
    expect(screen.queryByText('Ani reads my Telegram')).toBeNull();
  });

  it('links the account', async () => {
    mount({ configured: true, linked: false, elsewhere: false, link: null });
    fireEvent.click(await screen.findByText('Ani reads my Telegram'));
    await waitFor(() => expect(api.putJson).toHaveBeenCalledWith('/api/v1/telegram'));
  });

  it('unlinks only after the second press', async () => {
    mount({
      configured: true,
      linked: true,
      elsewhere: false,
      link: {
        me_id: '1389231240',
        username: 'moveeeax',
        polled_at: null,
        last_error: null,
        created_at: '2026-10-29T10:00:00Z',
      },
    });
    expect(await screen.findByText(/Linked to @moveeeax/)).toBeTruthy();
    fireEvent.click(screen.getByText('Unlink…'));
    expect(api.deleteJson).not.toHaveBeenCalled();
    fireEvent.click(screen.getByText('Unlink'));
    await waitFor(() => expect(api.deleteJson).toHaveBeenCalledWith('/api/v1/telegram'));
  });

  it('turns on her voice messages to Saved Messages', async () => {
    mount({
      configured: true,
      linked: true,
      elsewhere: false,
      link: {
        me_id: '1389231240',
        username: 'moveeeax',
        polled_at: null,
        last_error: null,
        created_at: '2026-10-29T10:00:00Z',
      },
    });
    fireEvent.click(await screen.findByLabelText(/voice message to Saved Messages/));
    await waitFor(() =>
      expect(api.putJson).toHaveBeenCalledWith('/api/v1/telegram/settings', {
        body: { voice_to_self: true },
      }),
    );
  });
});
