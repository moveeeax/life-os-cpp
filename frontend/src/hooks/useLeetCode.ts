import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type LeetCodeProfile = components['schemas']['LeetCodeProfile'];

const PATH = '/api/v1/leetcode/profile';

/** GET /leetcode/profile: the linked username and its counts, or null. */
export function useLeetCodeProfile() {
  return useQuery({
    queryKey: qk.leetcode.profile(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: LeetCodeProfile | null }>(PATH, { signal })).data,
  });
}

/** PUT /leetcode/profile: link a username; the day starts in the browser's zone. */
export function useSaveLeetCodeProfile() {
  return useApiMutation(
    async (username: string) =>
      (
        await api.putJson<{ data: LeetCodeProfile }>(PATH, {
          body: { username, tz: Intl.DateTimeFormat().resolvedOptions().timeZone },
        })
      ).data,
    { invalidate: [qk.leetcode.profile()] },
  );
}
