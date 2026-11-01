// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api, ApiClientError } from '@/lib/api/client';

import { Board } from './Board';

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function mount(kind: 'water' | 'protein' | 'telegram' | 'calendar_week', fail?: ApiClientError) {
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (fail) throw fail;
    if (path === '/api/v1/food/water')
      return { data: { date: '2026-10-31', ml: 1000, goal_ml: 2000 } };
    if (path === '/api/v1/food/day')
      return { data: { totals: { protein_g: 62.4 }, targets: { protein_g: 140 } } };
    if (path === '/api/v1/food/recent')
      return {
        data: [
          { id: 'skyr', name: 'Skyr', protein_g: 11, servings: [{ label: 'cup', grams: 150 }] },
        ],
      };
    return { data: [] };
  });
  const post = vi
    .spyOn(api, 'postJson')
    .mockResolvedValue({ data: { date: '2026-10-31', ml: 1250, goal_ml: 2000 } });
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <Board widgets={[{ kind }]} today={undefined} date="2026-10-31" tz="Asia/Tashkent" />
    </QueryClientProvider>,
  );
  return post;
}

describe('Board', () => {
  it('WaterGlassInOneTap: a glass adds 250 ml to the day', async () => {
    const post = mount('water');
    expect(await screen.findByText('1 of 2 l')).toBeTruthy();
    fireEvent.click(screen.getByText('+ 250 ml'));
    await waitFor(() =>
      expect(post).toHaveBeenCalledWith('/api/v1/food/water', {
        body: { date: '2026-10-31', add_ml: 250 },
      }),
    );
    expect(await screen.findByText('1.25 of 2 l')).toBeTruthy();
  });

  it('ProteinInOneTap: a recent rich product goes to the diary with its serving', async () => {
    const post = mount('protein');
    expect(await screen.findByText('62 of 140 g')).toBeTruthy();
    fireEvent.click(await screen.findByText('+ Skyr'));
    await waitFor(() =>
      expect(post).toHaveBeenCalledWith('/api/v1/food/entries', {
        body: expect.objectContaining({ date: '2026-10-31', item_id: 'skyr', grams: 150 }),
      }),
    );
  });

  const failWith = (status: number, code: string) =>
    new ApiClientError({ status, message: code, code });

  it('LinkStateTelegram: "Not linked" only for the 404 of an unlinked account', async () => {
    mount('telegram', failWith(404, 'telegram_not_linked'));
    expect(await screen.findByText('Not linked')).toBeTruthy();
  });

  it('LinkStateTelegram: a 429 is not "Not linked"', async () => {
    mount('telegram', failWith(429, 'rate_limited'));
    expect(await screen.findByText('—')).toBeTruthy();
    expect(screen.queryByText('Not linked')).toBeNull();
  });

  it('LinkStateCalendar: "Not connected" for 404 and 409, a dash for a 500', async () => {
    mount('calendar_week', failWith(409, 'calendar_disconnected'));
    expect(await screen.findByText('Not connected')).toBeTruthy();
    cleanup();
    vi.restoreAllMocks();
    mount('calendar_week', failWith(500, 'internal'));
    expect(await screen.findByText('—')).toBeTruthy();
    expect(screen.queryByText('Not connected')).toBeNull();
  });
});
