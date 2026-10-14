import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

/** Answer of the public GET /api/v1/auth/registration. */
interface RegistrationState {
  data: { open: boolean };
}

/**
 * Whether anyone may sign up on this server (closed means invite-only).
 * While unknown, or when the server does not answer the probe, sign-up
 * counts as open: the register call itself still has the last word.
 */
export function useRegistrationOpen(): boolean {
  const probe = useQuery({
    queryKey: qk.registration(),
    staleTime: 10 * 60_000,
    retry: false,
    queryFn: async ({ signal }) => {
      const { data } = await api.GET('/api/v1/auth/registration' as string, { signal });
      const open = (data as Partial<RegistrationState> | undefined)?.data?.open;
      return typeof open === 'boolean' ? open : true;
    },
  });
  return probe.data ?? true;
}
