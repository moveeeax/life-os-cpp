// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api, ApiClientError } from '@/lib/api/client';

import type { Today } from '@/lib/now/types';

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

  it('ExpiringDocuments: the soonest countdown, a name and a date per row, the rest counted', () => {
    const doc = (
      id: string,
      holder: string,
      kind: 'passport' | 'visa',
      valid_until: string,
      days_left: number,
    ) => ({
      id,
      holder,
      kind,
      valid_until,
      days_left,
    });
    const today = {
      focus: null,
      note: null,
      errors: [],
      documents: [
        doc('a', 'Anna', 'passport', '2026-10-30', -1),
        doc('b', 'Boris', 'visa', '2026-12-31', 61),
        doc('c', 'Anna', 'visa', '2027-02-01', 93),
        doc('d', 'Boris', 'passport', '2027-04-01', 152),
      ],
    } as Today;
    const qc = new QueryClient();
    render(
      <QueryClientProvider client={qc}>
        <Board
          widgets={[{ kind: 'documents' }]}
          today={today}
          date="2026-10-31"
          tz="Asia/Tashkent"
        />
      </QueryClientProvider>,
    );
    expect(screen.getByText('ran out')).toBeTruthy();
    expect(screen.getByText("Anna's passport")).toBeTruthy();
    expect(screen.getByText('Dec 31')).toBeTruthy();
    expect(screen.getByText('Feb 1, 2027')).toBeTruthy();
    expect(screen.queryByText("Boris's passport")).toBeNull();
    expect(screen.getByText('and 1 more')).toBeTruthy();
  });

  it('ExpiringDocuments: nothing expiring, and a dash when the part failed', () => {
    const qc = new QueryClient();
    const mountWith = (t: Partial<Today>) =>
      render(
        <QueryClientProvider client={qc}>
          <Board
            widgets={[{ kind: 'documents' }]}
            today={{ focus: null, note: null, errors: [], ...t } as Today}
            date="2026-10-31"
            tz="Asia/Tashkent"
          />
        </QueryClientProvider>,
      );
    mountWith({});
    expect(screen.getByText('Nothing expiring')).toBeTruthy();
    cleanup();
    mountWith({ errors: ['documents'] });
    expect(screen.getByText('—')).toBeTruthy();
  });
});
