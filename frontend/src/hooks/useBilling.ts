import { keepPreviousData, useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

/** GET /billing/packages: the packages on sale, with the rate and the bounds. */
export function useBillingPackages() {
  return useQuery({
    queryKey: qk.billing.packages(),
    queryFn: () => api.getJson('/api/v1/billing/packages'),
  });
}

/** GET /billing/wallet: the balance and one page of its history. */
export function useWalletPage(page: number, limit: number, offset: number) {
  return useQuery({
    queryKey: qk.billing.wallet(page),
    queryFn: () => api.getJson('/api/v1/billing/wallet', { query: { limit, offset } }),
    placeholderData: keepPreviousData,
  });
}

/** POST /billing/topup: a package or an amount; @p onApprove gets PayPal's approve URL. */
export function useTopup(onApprove: (url: string) => void) {
  return useApiMutation(
    (vars: { package_id?: string; amount_cents?: number }) =>
      api.postJson('/api/v1/billing/topup', { body: vars }),
    { onSuccess: (res) => onApprove(res.data.approve_url) },
  );
}
