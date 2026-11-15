import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { usePagedQuery } from '@/hooks/usePagedQuery';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { DlqListResponse } from '@/lib/api/types';

/** Both admin job lists refresh every five seconds while open. */
const REFRESH_MS = 5000;

/** GET /jobs, a page at a time, optionally of one type. */
export function useAdminJobs(typeFilter: string, perPage: number) {
  return usePagedQuery({
    queryKey: qk.admin.jobs(typeFilter),
    queryFn: ({ limit, offset }) =>
      api.getJson('/api/v1/jobs', {
        query: { limit, offset, ...(typeFilter ? { type: typeFilter } : {}) },
      }),
    perPage,
    refetchInterval: REFRESH_MS,
  });
}

/** GET /jobs/dlq: the newest 100 dead-lettered jobs. */
export function useJobsDlq() {
  return useQuery({
    queryKey: qk.admin.jobsDlq(),
    queryFn: () => api.getJson<DlqListResponse>('/api/v1/jobs/dlq?limit=100'),
    refetchInterval: REFRESH_MS,
  });
}

/** POST /jobs/dlq/{id}/requeue. */
export function useRequeueJob() {
  return useApiMutation(
    (id: string) => api.postJson(`/api/v1/jobs/dlq/${id}/requeue`, { body: {} }),
    // Refresh the DLQ so the requeued row disappears (preventing a double
    // requeue); also bump the jobs list since the job is back in flight.
    { invalidate: [qk.admin.jobsDlq(), qk.admin.jobs()] },
  );
}
