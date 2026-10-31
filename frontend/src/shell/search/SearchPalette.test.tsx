// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { MemoryRouter, Route, Routes, useLocation } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';

import { SearchPalette } from './SearchPalette';

vi.mock('../useGlassNav', () => ({
  useGlassNav: () => ({
    items: [{ key: 'money', label: 'Money', to: '/money' }],
    nowOn: true,
    settled: true,
  }),
}));

function Where() {
  return <span data-testid="where">{useLocation().pathname + useLocation().search}</span>;
}

function mount() {
  vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/tasks/items')
      return { data: [{ id: 't1', title: 'Pay rent', due: '2026-10-30', status: 'open' }] };
    if (path === '/api/v1/notes') return { data: [] };
    if (path === '/api/v1/money/transactions') return { data: [] };
    return { data: [] };
  });
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={['/now']}>
        <SearchPalette />
        <Routes>
          <Route path="*" element={<Where />} />
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('SearchPalette', () => {
  it('opens on Cmd+K, finds a task and opens it with Enter', async () => {
    mount();
    fireEvent.keyDown(window, { key: 'k', metaKey: true });
    fireEvent.change(await screen.findByRole('combobox', { name: 'Search Life OS' }), {
      target: { value: 'rent' },
    });
    expect(await screen.findByText('Pay rent')).toBeTruthy();
    await waitFor(() =>
      expect(api.getJson).toHaveBeenCalledWith(
        '/api/v1/tasks/items',
        expect.objectContaining({ query: expect.objectContaining({ q: 'rent' }) }),
      ),
    );
    fireEvent.keyDown(screen.getByRole('combobox'), { key: 'Enter' });
    await waitFor(() =>
      expect(screen.getByTestId('where').textContent).toMatch(/^\/tasks.*task=t1/),
    );
    expect(screen.queryByRole('combobox')).toBeNull();
  });

  it('jumps to a page by its name and closes on Escape', async () => {
    mount();
    fireEvent.keyDown(window, { key: 'k', ctrlKey: true });
    const box = await screen.findByRole('combobox', { name: 'Search Life OS' });
    fireEvent.change(box, { target: { value: 'mon' } });
    expect(await screen.findByText('Money')).toBeTruthy();
    fireEvent.keyDown(box, { key: 'Escape' });
    expect(screen.queryByRole('combobox')).toBeNull();
  });
});
