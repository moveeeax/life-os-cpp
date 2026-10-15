// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { NowSettings } from '@/lib/now';

import { ArrangeSheet } from './ArrangeSheet';

/**
 * Arrange Now: the four slots are the dock order and never offer Tasks; the
 * links block edits up to eight rows, drops the empty ones on save, and
 * refuses a URL that is neither a path nor http(s).
 */

const mutate = vi.fn();
vi.mock('@/hooks/useNow', () => ({
  useSaveNowSettings: () => ({ mutate, isPending: false, error: null }),
}));
vi.mock('@/hooks/useHabits', () => ({ useHabitsEnabled: () => true }));

const settings: NowSettings = {
  city: null,
  country_code: null,
  latitude: null,
  longitude: null,
  timezone: null,
  corners: { tl: 'body', tr: 'goals', bl: 'day', br: 'habits' },
  links: [{ title: 'Summary', url: '/money/summary' }],
};

function renderSheet() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <ArrangeSheet settings={settings} onClose={() => {}} />
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  mutate.mockClear();
});

describe('the dock slots', () => {
  it('are labelled as the dock order and never offer Tasks', () => {
    renderSheet();
    const first = screen.getByLabelText('Dock, first') as HTMLSelectElement;
    expect(first.value).toBe('body');
    const options = Array.from(first.options).map((o) => o.textContent);
    expect(options).toEqual(['Nothing', 'Body', 'Goals', 'Food and money', 'Habits']);
    expect((screen.getByLabelText('Dock, fourth') as HTMLSelectElement).value).toBe('habits');
  });
});

describe('the links', () => {
  it('start from the saved rows, add one, drop an empty one and save the clean list', () => {
    renderSheet();
    expect((screen.getByDisplayValue('Summary') as HTMLInputElement).value).toBe('Summary');
    fireEvent.click(screen.getByRole('button', { name: 'Add a link' }));
    fireEvent.click(screen.getByRole('button', { name: 'Add a link' }));
    const titles = screen.getAllByLabelText('Title');
    const urls = screen.getAllByLabelText('URL or path');
    expect(titles).toHaveLength(3);
    fireEvent.change(titles[1], { target: { value: '  Bank ' } });
    fireEvent.change(urls[1], { target: { value: 'https://bank.example/' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate).toHaveBeenCalledTimes(1);
    expect(mutate.mock.calls[0][0]).toEqual({
      corners: settings.corners,
      links: [
        { title: 'Summary', url: '/money/summary' },
        { title: 'Bank', url: 'https://bank.example/' },
      ],
    });
  });

  it('turns the Tasks slot of an older layout into an empty one on save', () => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    render(
      <QueryClientProvider client={qc}>
        <ArrangeSheet
          settings={{ ...settings, corners: { tl: 'body', tr: 'goals', bl: 'day', br: 'tasks' } }}
          onClose={() => {}}
        />
      </QueryClientProvider>,
    );
    expect((screen.getByLabelText('Dock, fourth') as HTMLSelectElement).value).toBe('');
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate.mock.calls[0][0].corners).toEqual({
      tl: 'body',
      tr: 'goals',
      bl: 'day',
      br: null,
    });
  });

  it('sends no links when they did not change', () => {
    renderSheet();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate.mock.calls[0][0]).toEqual({ corners: settings.corners });
  });

  it('refuses a URL that is neither a path nor http(s)', () => {
    renderSheet();
    fireEvent.change(screen.getByLabelText('URL or path'), {
      target: { value: 'javascript:alert(1)' },
    });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate).not.toHaveBeenCalled();
    expect(screen.getByText('A path like /money or an http(s) address.')).toBeTruthy();
  });

  it('refuses a half-filled row instead of dropping it', () => {
    renderSheet();
    fireEvent.click(screen.getByRole('button', { name: 'Add a link' }));
    fireEvent.change(screen.getAllByLabelText('Title')[1], { target: { value: 'Bank' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate).not.toHaveBeenCalled();
    expect(screen.getByText('A title and a URL, or an empty row.')).toBeTruthy();
  });

  it('keeps the other rows in place when one is removed', () => {
    renderSheet();
    fireEvent.click(screen.getByRole('button', { name: 'Add a link' }));
    fireEvent.change(screen.getAllByLabelText('Title')[1], { target: { value: 'Bank' } });
    fireEvent.change(screen.getAllByLabelText('URL or path')[1], {
      target: { value: 'https://bank.example/' },
    });
    const bank = screen.getByDisplayValue('Bank');
    fireEvent.click(screen.getByRole('button', { name: 'Remove Summary' }));
    expect(screen.getByDisplayValue('Bank')).toBe(bank);
  });

  it('removes a row', () => {
    renderSheet();
    fireEvent.click(screen.getByRole('button', { name: 'Remove Summary' }));
    expect(screen.queryByDisplayValue('Summary')).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));
    expect(mutate.mock.calls[0][0]).toEqual({ corners: settings.corners, links: [] });
  });
});
