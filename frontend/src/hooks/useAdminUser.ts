import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { UserDetailResponse } from '@/lib/api/types';

/** GET /admin/users/{id}. */
export function useAdminUser(id: string) {
  return useQuery({
    queryKey: qk.admin.user(id),
    queryFn: () => api.getJson<UserDetailResponse>('/api/v1/admin/users/' + id),
  });
}

/** PATCH /admin/users/{id}: the changed fields only. */
export function useUpdateAdminUser(id: string, onSuccess?: () => void) {
  return useApiMutation(
    (patch: Record<string, unknown>) =>
      api.patchJson<UserDetailResponse>('/api/v1/admin/users/' + id, { body: patch }),
    { invalidate: [qk.admin.user(id), qk.admin.users()], onSuccess },
  );
}

/** DELETE /admin/users/{id}. */
export function useDeleteAdminUser(id: string, onSuccess?: () => void) {
  return useApiMutation(() => api.deleteJson('/api/v1/admin/users/' + id), {
    invalidate: [qk.admin.users()],
    onSuccess,
  });
}
