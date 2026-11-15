import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { usePagedQuery } from '@/hooks/usePagedQuery';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type {
  AdminPackageListResponse,
  AdminPackageResponse,
  AdminPaymentListResponse,
  BillingSettingsResponse,
  MessageResponse,
  Payment,
} from '@/lib/api/types';

const BASE = '/api/v1/admin/billing';
const PACKAGES = [qk.admin.billing.packages()];

/** What the package form sends on create and on edit. */
export interface PackageBody {
  title: string;
  amount_cents: number;
  credits: number;
  active: boolean;
  sort: number;
}

/** GET /admin/billing/packages: every package, inactive ones too. */
export function useAdminPackages() {
  return useQuery({
    queryKey: qk.admin.billing.packages(),
    queryFn: () => api.getJson<AdminPackageListResponse>(`${BASE}/packages`),
  });
}

/** POST /admin/billing/packages. */
export function useCreatePackage(onSuccess?: () => void) {
  return useApiMutation(
    (body: PackageBody) => api.postJson<AdminPackageResponse>(`${BASE}/packages`, { body }),
    { invalidate: PACKAGES, onSuccess },
  );
}

/** PATCH /admin/billing/packages/{id}. */
export function useUpdatePackage(onSuccess?: () => void) {
  return useApiMutation(
    (vars: { id: string; body: PackageBody }) =>
      api.patchJson<AdminPackageResponse>(`${BASE}/packages/${vars.id}`, { body: vars.body }),
    { invalidate: PACKAGES, onSuccess },
  );
}

/** DELETE /admin/billing/packages/{id}. */
export function useDeletePackage(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<MessageResponse>(`${BASE}/packages/${id}`), {
    invalidate: PACKAGES,
    onSuccess,
  });
}

/** GET /admin/billing/payments, a page at a time, optionally of one status. */
export function useAdminPayments(status: '' | Payment['status'], perPage: number) {
  return usePagedQuery({
    queryKey: qk.admin.billing.payments(status),
    queryFn: ({ limit, offset }) =>
      api.getJson<AdminPaymentListResponse>(`${BASE}/payments`, {
        query: { limit, offset, ...(status ? { status } : {}) },
      }),
    perPage,
  });
}

/** GET /admin/billing/settings: the rate and the top-up bounds. */
export function useBillingSettings() {
  return useQuery({
    queryKey: qk.admin.billing.settings(),
    queryFn: () => api.getJson<BillingSettingsResponse>(`${BASE}/settings`),
  });
}

/** PUT /admin/billing/settings. */
export function useSaveBillingSettings(onSuccess?: () => void) {
  return useApiMutation(
    (body: { credits_per_unit: number; min_amount_cents: number; max_amount_cents: number }) =>
      api.putJson<BillingSettingsResponse>(`${BASE}/settings`, { body }),
    { invalidate: [qk.admin.billing.settings()], onSuccess },
  );
}
