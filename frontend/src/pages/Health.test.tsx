// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';

/** The chart rows of Health: at most two open, a third closes the oldest. */

const ok = <T,>(data: T) => ({
  data,
  isPending: false,
  isError: false,
  isSuccess: true,
  error: null,
  refetch: vi.fn(),
});

vi.mock('@/hooks/useHealth', async () => {
  const actual = await vi.importActual<typeof import('@/hooks/useHealth')>('@/hooks/useHealth');
  const none = () => ok([]);
  return {
    ...actual,
    useSummary: none,
    useActivity: none,
    useSleep: none,
    useHeartRate: none,
    useStress: none,
    useSpo2: none,
    useBody: none,
    useWorkouts: none,
  };
});

vi.mock('@/hooks/useMe', () => ({
  useMe: () =>
    ok({
      id: 'u1',
      email: 'me@example.com',
      full_name: 'Me',
      confirmed: true,
      role_id: 2,
      role: { id: 2, name: 'reader', permissions: 0x04 },
    }),
}));

vi.mock('@/hooks/useMiAccount', () => ({
  useMiAccount: () => ok({ status: 'ok' }),
}));

vi.mock('./health/LazyTimeChart', () => ({
  TimeChart: () => <div className="num-chart" />,
}));

import { HealthPage } from './Health';

afterEach(cleanup);

const head = (title: string) => screen.getByRole('button', { name: new RegExp(`^${title}`) });
const isOpen = (title: string) => head(title).getAttribute('aria-expanded') === 'true';

describe('Health chart rows', () => {
  it('keep two open: a third closes the one opened first', () => {
    const qc = new QueryClient();
    render(
      <QueryClientProvider client={qc}>
        <ToastProvider>
          <MemoryRouter initialEntries={['/health?from=2026-10-12&to=2026-10-16']}>
            <HealthPage />
          </MemoryRouter>
        </ToastProvider>
      </QueryClientProvider>,
    );
    expect(isOpen('Sleep')).toBe(false);
    fireEvent.click(head('Sleep'));
    fireEvent.click(head('Heart rate'));
    expect([isOpen('Sleep'), isOpen('Heart rate')]).toEqual([true, true]);

    fireEvent.click(head('Stress'));
    expect([isOpen('Sleep'), isOpen('Heart rate'), isOpen('Stress')]).toEqual([false, true, true]);

    // Closing one by hand leaves room: the next opens without closing another.
    fireEvent.click(head('Heart rate'));
    fireEvent.click(head('Weight and body fat'));
    expect([isOpen('Heart rate'), isOpen('Stress'), isOpen('Weight and body fat')]).toEqual([
      false,
      true,
      true,
    ]);
  });
});
