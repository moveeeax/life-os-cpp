// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import { DEFAULT_CORNERS, type Today } from '@/lib/now';

import { Corners } from './Corners';

/**
 * The dock and the corners of Now: the dock lists the placed chips without
 * Tasks, one panel is open at a time whoever opened it, Escape returns the
 * focus to the opener, and a link row is an anchor only for a path or an
 * http(s) URL.
 */

vi.mock('@/pages/tasks/Capture', () => ({
  Capture: () => <input aria-label="New task in one phrase" />,
}));
vi.mock('@/hooks/useHabits', () => ({
  useHabitsEnabled: () => true,
  useHabitDays: () => ({ data: [] }),
  useTickHabit: () => ({ mutate: vi.fn(), isPending: false, error: null }),
}));

const today: Today = {
  tasks: {
    late_count: 1,
    first_late: { id: 't1', title: 'Dispute the charge', due: '2026-10-12' },
    late: [{ id: 't1', title: 'Dispute the charge', due: '2026-10-12' }],
    today: [{ id: 't2', title: 'Habits spec review', status: 'open' }],
    inbox_count: 0,
  },
  goals: [],
  focus: null,
  errors: [],
};

function renderCorners(props: Partial<Parameters<typeof Corners>[0]> = {}) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter>
          <Corners
            today={today}
            hour={16}
            date="2026-10-15"
            corners={DEFAULT_CORNERS}
            links={[]}
            {...props}
          />
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

afterEach(cleanup);

describe('the dock', () => {
  it('lists the placed chips in slot order and never Tasks', () => {
    renderCorners();
    const tiles = screen.getAllByRole('button', { name: /^(Body|Goals|Food and money|Habits)/ });
    expect(tiles.map((t) => t.querySelector('.c-k')?.textContent?.trim())).toEqual([
      'Body',
      'Goals',
      'Food and money',
    ]);
    expect(screen.getByRole('button', { name: /\bTasks\b/ }).getAttribute('aria-expanded')).toBe(
      'false',
    );
  });
});

describe('the corners', () => {
  it('point aria-controls only at a panel that is open', () => {
    renderCorners();
    const body = screen.getByRole('button', { name: /^Body/ });
    expect(body.getAttribute('aria-controls')).toBeNull();
    fireEvent.click(body);
    const id = body.getAttribute('aria-controls');
    expect(id).toBeTruthy();
    expect(document.getElementById(id ?? '')).toBe(screen.getByRole('dialog', { name: 'Body' }));
  });

  it('open one panel at a time and give the focus back on Escape', () => {
    renderCorners();
    fireEvent.click(screen.getByRole('button', { name: /^Body/ }));
    expect(screen.getByRole('dialog', { name: 'Body' })).toBeTruthy();
    const add = screen.getByRole('button', { name: 'Add' });
    fireEvent.click(add);
    expect(screen.queryByRole('dialog', { name: 'Body' })).toBeNull();
    expect(screen.getByRole('dialog', { name: 'Add' })).toBeTruthy();
    expect(screen.getAllByRole('dialog')).toHaveLength(1);
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(screen.queryByRole('dialog')).toBeNull();
    return new Promise<void>((done) =>
      requestAnimationFrame(() => {
        expect(document.activeElement).toBe(add);
        done();
      }),
    );
  });

  it('shows the Tasks panel from its corner with the counts on the opener', () => {
    renderCorners();
    const tasks = screen.getByRole('button', { name: /\bTasks\b/ });
    expect(tasks.textContent).toContain('1 today');
    expect(tasks.textContent).toContain('1 late');
    fireEvent.click(tasks);
    expect(screen.getByRole('dialog', { name: 'Tasks' })).toBeTruthy();
    expect(screen.getByText('Dispute the charge')).toBeTruthy();
  });

  it('links a path through the router, an URL in a new tab, and nothing else', () => {
    renderCorners({
      links: [
        { title: 'Summary', url: '/money/summary' },
        { title: 'Bank', url: 'https://bank.example/' },
        { title: 'Odd', url: 'javascript:alert(1)' },
      ],
    });
    fireEvent.click(screen.getByRole('button', { name: 'Links' }));
    const summary = screen.getByRole('link', { name: 'Summary' });
    expect(summary.getAttribute('href')).toBe('/money/summary');
    const bank = screen.getByRole('link', { name: /Bank/ });
    expect(bank.getAttribute('target')).toBe('_blank');
    expect(bank.getAttribute('rel')).toContain('noopener');
    expect(screen.queryByRole('link', { name: 'Odd' })).toBeNull();
    expect(screen.getByText('Odd')).toBeTruthy();
  });

  it('says where links come from when there are none', () => {
    renderCorners();
    fireEvent.click(screen.getByRole('button', { name: 'Links' }));
    expect(screen.getByText('No links yet. Add them in Arrange.')).toBeTruthy();
  });
});
