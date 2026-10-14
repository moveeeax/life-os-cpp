import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

/**
 * Whether anyone may sign up on this server (closed means invite-only).
 * While unknown, or when the server does not answer the probe, sign-up
 * counts as open: the register call itself still has the last word.
 */
export function useRegistrationOpen(enabled = true): boolean {
  const probe = useQuery({
    queryKey: qk.registration(),
    enabled,
    staleTime: 10 * 60_000,
    retry: false,
    queryFn: async ({ signal }) => {
      const { data } = await api.GET('/api/v1/auth/registration', { signal });
      return data?.data.open ?? true;
    },
  });
  return probe.data ?? true;
}
