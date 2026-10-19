import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';

import { DataTable, type Column } from '@/components/DataTable';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { BillingMetricsResponse, SkippedRefund } from '@/lib/api/types';
import { formatDateTime } from '@/lib/dates';
import { formatCents } from '@/lib/money';
import { Num } from '@/shell/kit/Num';

type Period = 'day' | 'week' | 'month';

const OUTCOME_LABELS: Record<SkippedRefund['outcome'], string> = {
  skipped_insufficient: 'Credits already spent',
  skipped_zero_credits: 'Too small for a credit',
  skipped_not_credited: 'Payment never credited',
};

/**
 * Refunds PayPal paid out that took no credits back (billing_refunds rows
 * with a skipped_* outcome). A chargeback after the buyer spent the credits
 * lands here as "Credits already spent": the money is gone and the wallet
 * kept the credits, so it needs a manual look (an adjustment, or a note).
 */
export function SkippedRefundsTab() {
  const [period, setPeriod] = useState<Period>('month');
  const { data, isLoading, error } = useQuery({
    queryKey: qk.admin.billing.metrics(period),
    queryFn: () =>
      api.getJson<BillingMetricsResponse>('/api/v1/admin/billing/metrics', {
        query: { period },
      }),
  });
  const m = data?.data;

  const columns: Column<SkippedRefund>[] = [
    { header: 'When', cell: (r) => formatDateTime(r.created_at) },
    { header: 'User', cell: (r) => r.email || r.user_id },
    {
      header: 'Amount',
      className: 'font-mono',
      cell: (r) => <Num>${formatCents(r.amount_cents)}</Num>,
    },
    { header: 'Why', cell: (r) => OUTCOME_LABELS[r.outcome] ?? r.outcome },
    { header: 'Refund', className: 'font-mono text-xs', cell: (r) => r.provider_refund_id },
  ];

  return (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between space-y-0">
        <CardTitle>Refunds that took no credits back</CardTitle>
        <select
          aria-label="Period"
          className="rounded border bg-background px-2 py-1 text-sm"
          value={period}
          onChange={(e) => setPeriod(e.target.value as Period)}
        >
          <option value="day">Last day</option>
          <option value="week">Last week</option>
          <option value="month">Last month</option>
        </select>
      </CardHeader>
      <CardContent className="space-y-3">
        {error ? (
          <p className="text-sm text-destructive">Could not load the refunds.</p>
        ) : isLoading || !m ? (
          <p className="text-sm text-muted-foreground">Loading…</p>
        ) : m.refunds_skipped_count === 0 ? (
          <p className="text-sm text-muted-foreground">None in this period.</p>
        ) : (
          <>
            <p className="text-sm text-muted-foreground">
              <Num>{m.refunds_skipped_count}</Num> refund(s),{' '}
              <Num>${formatCents(m.refunds_skipped_cents)}</Num> paid out without taking credits
              back.
            </p>
            <DataTable
              columns={columns}
              rows={m.refunds_skipped}
              rowKey={(r) => r.provider_refund_id}
            />
          </>
        )}
      </CardContent>
    </Card>
  );
}
