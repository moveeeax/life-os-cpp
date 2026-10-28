// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';
import type { Today } from '@/lib/now/types';

import { WeeklyReview } from './WeeklyReview';

const today: Today = {
  tasks: {
    late_count: 1,
    first_late: null,
    late: [{ id: 'l1', title: 'Сдать отчет', due: '2026-10-20' }],
    today: [],
    inbox_count: 0,
  },
  goals: [
    {
      id: 'g1',
      title: 'Бег 100 км',
      kind: 'count',
      due: '2026-12-01',
      pace: 'behind',
      progress: 0.2,
      days_left: 30,
      unit: 'км',
      detail: {} as never,
    },
  ],
  habits: { done: 0, total: 1, left: [], week: [{ name: 'Вода', done: 3, per_week: 7 }] },
  focus: null,
  errors: [],
};

beforeEach(() => {
  localStorage.clear();
  vi.spyOn(api, 'patchJson').mockResolvedValue({ data: {} });
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

function show() {
  const qc = new QueryClient();
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <WeeklyReview today={today} date="2026-11-01" tz="Asia/Almaty" onClose={() => {}} />
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

describe('WeeklyReview', () => {
  it('lists late tasks, goals behind and habits under their week', () => {
    show();
    expect(screen.getByText('Сдать отчет')).toBeTruthy();
    expect(screen.getByText('Бег 100 км')).toBeTruthy();
    expect(screen.getByText('Вода')).toBeTruthy();
  });

  it('moves a late task to tomorrow and closes its row for the week', async () => {
    show();
    fireEvent.click(screen.getByRole('button', { name: 'Tomorrow: Сдать отчет' }));
    await waitFor(() =>
      expect(api.patchJson).toHaveBeenCalledWith('/api/v1/tasks/items/l1', {
        body: { due: '2026-11-02' },
        query: { tz: 'Asia/Almaty' },
      }),
    );
    await waitFor(() => expect(screen.queryByText('Сдать отчет')).toBeNull());
    cleanup();
    show();
    expect(screen.queryByText('Сдать отчет')).toBeNull();
  });

  it('marks a late task done', async () => {
    show();
    fireEvent.click(screen.getByRole('button', { name: 'Done: Сдать отчет' }));
    await waitFor(() =>
      expect(api.patchJson).toHaveBeenCalledWith('/api/v1/tasks/items/l1', {
        body: { status: 'done' },
        query: { tz: 'Asia/Almaty' },
      }),
    );
  });

  it('says so when a move fails', async () => {
    vi.spyOn(api, 'patchJson').mockRejectedValue(new Error('offline'));
    show();
    fireEvent.click(screen.getByRole('button', { name: 'Tomorrow: Сдать отчет' }));
    expect(await screen.findByRole('alert')).toBeTruthy();
    expect(screen.getByText('Сдать отчет')).toBeTruthy();
  });
});
