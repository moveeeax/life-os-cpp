// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { BillingMetricsResponse } from '@/lib/api/types';

import { SkippedRefundsTab } from './SkippedRefunds';

/**
 * The admin list of refunds that took no credits back: a chargeback after
 * the credits were spent shows with its user, amount and reason, and the
 * period select asks the metrics for that window.
 */

const getJson = vi.fn();
vi.mock('@/lib/api/client', () => ({
  api: { getJson: (...args: unknown[]) => getJson(...args) },
  apiErrorMessage: (_e: unknown, fallback: string) => fallback,
}));

function metrics(over: Partial<BillingMetricsResponse['data']> = {}): BillingMetricsResponse {
  return {
    data: {
      period: 'month',
      revenue_cents: 2000,
      payments_count: 1,
      avg_payment_cents: 2000,
      conversion: { created: 1, captured: 1, rate: 1 },
      refunds_cents: 0,
      refunds_count: 0,
      refunds_skipped_cents: 2000,
      refunds_skipped_count: 1,
      refunds_skipped: [
        {
          provider_refund_id: 'REFUND-CB-1',
          payment_id: '11111111-1111-4111-8111-111111111111',
          user_id: '22222222-2222-4222-8222-222222222222',
          email: 'bob@example.com',
          amount_cents: 2000,
          outcome: 'skipped_insufficient',
          created_at: '2026-10-17T09:30:00Z',
        },
      ],
      outstanding_credits: 0,
      outstanding_value_cents: 0,
      series: [],
      top_packages: [],
      top_users: [],
      ...over,
    },
  };
}

function renderTab() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <SkippedRefundsTab />
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  getJson.mockReset();
});

describe('SkippedRefundsTab', () => {
  it('lists a refund that took no credits back', async () => {
    getJson.mockResolvedValue(metrics());
    renderTab();
    expect(await screen.findByText('bob@example.com')).toBeTruthy();
    expect(screen.getByText('Credits already spent')).toBeTruthy();
    expect(screen.getByText('REFUND-CB-1')).toBeTruthy();
    expect(getJson).toHaveBeenCalledWith('/api/v1/admin/billing/metrics', {
      query: { period: 'month' },
    });
    // The amounts are marked for Hide numbers.
    for (const el of screen.getAllByText('$20.00')) {
      expect(el.closest('.num')).not.toBeNull();
    }
  });

  it('says so when there is none, and asks again for another period', async () => {
    getJson.mockResolvedValue(
      metrics({ refunds_skipped: [], refunds_skipped_count: 0, refunds_skipped_cents: 0 }),
    );
    renderTab();
    expect(await screen.findByText('None in this period.')).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Period'), { target: { value: 'week' } });
    await screen.findByText('None in this period.');
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/admin/billing/metrics', {
      query: { period: 'week' },
    });
  });
});
