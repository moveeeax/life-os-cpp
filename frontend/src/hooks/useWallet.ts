import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

/**
 * The wallet balance, which doubles as the billing switch: the call 404s
 * while billing is off on the server, so a billing link shows only when this
 * query succeeded.
 */
export function useWalletSummary(signedIn: boolean) {
  return useQuery({
    queryKey: qk.billing.wallet(),
    queryFn: () => api.getJson('/api/v1/billing/wallet', { query: { limit: 1 } }),
    enabled: signedIn,
  });
}
