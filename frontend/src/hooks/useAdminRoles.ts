import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Role } from '@/lib/api/types';

/**
 * Shared admin roles query — Roles, UserDetail and InviteUser pages all
 * need the same list under the same cache key. The response type is
 * inferred from the OpenAPI `paths` tree (RolesResponse).
 */
export function useAdminRoles() {
  return useQuery({
    queryKey: qk.admin.roles(),
    queryFn: () => api.getJson('/api/v1/admin/roles'),
  });
}

/** The fields the role form sends on create and on edit. */
export interface RoleFormState {
  name: string;
  permissions: number;
  is_default: boolean;
}

const WRITES = [qk.admin.roles()];

/** POST /admin/roles. */
export function useCreateRole(onSuccess?: () => void) {
  return useApiMutation(
    (form: RoleFormState) => api.postJson<{ data: Role }>('/api/v1/admin/roles', { body: form }),
    { invalidate: WRITES, onSuccess },
  );
}

/** PATCH /admin/roles/{id}. */
export function useUpdateRole(onSuccess?: () => void) {
  return useApiMutation(
    (vars: { id: number; form: RoleFormState }) =>
      api.patchJson<{ data: Role }>(`/api/v1/admin/roles/${vars.id}`, { body: vars.form }),
    { invalidate: WRITES, onSuccess },
  );
}

/** DELETE /admin/roles/{id}. */
export function useDeleteRole(onSuccess?: () => void) {
  return useApiMutation((id: number) => api.deleteJson(`/api/v1/admin/roles/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}
