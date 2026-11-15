// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';

/**
 * The admin billing page over its hooks: the packages tab lists what
 * useAdminPackages returned, a delete goes through the confirm to
 * useDeletePackage, and the settings tab saves through its hook.
 */

const { remove, save } = vi.hoisted(() => ({ remove: vi.fn(), save: vi.fn() }));
const idle = (mutate = vi.fn()) => ({ mutate, isPending: false, error: null });

vi.mock('@/hooks/useAdminBilling', () => ({
  useAdminPackages: () => ({
    data: {
      data: [
        {
          id: 'pk1',
          title: 'Starter',
          amount_cents: 500,
          credits: 500,
          active: true,
          sort: 0,
        },
      ],
    },
    isLoading: false,
    error: null,
  }),
  useCreatePackage: () => idle(),
  useUpdatePackage: () => idle(),
  useDeletePackage: () => idle(remove),
  useAdminPayments: () => ({
    data: { data: [], total: 0 },
    isLoading: false,
    error: null,
    isPlaceholderData: false,
    page: 1,
    setPage: () => {},
    totalPages: 1,
  }),
  useBillingSettings: () => ({
    data: { data: { credits_per_unit: 100, min_amount_cents: 100, max_amount_cents: 100000 } },
    isLoading: false,
    error: null,
  }),
  useSaveBillingSettings: () => idle(save),
}));

import { AdminBillingPage } from './Billing';

beforeEach(() => {
  remove.mockReset();
  save.mockReset();
});
afterEach(cleanup);

function renderPage() {
  render(
    <QueryClientProvider client={new QueryClient()}>
      <ToastProvider>
        <MemoryRouter>
          <AdminBillingPage />
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

describe('AdminBillingPage', () => {
  it('lists the packages and deletes one after the confirm', () => {
    renderPage();
    expect(screen.getByText('Starter')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: /delete/i }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete package' }));
    expect(remove).toHaveBeenCalledWith('pk1');
  });

  it('opens the settings tab', () => {
    renderPage();
    fireEvent.click(screen.getByRole('tab', { name: 'settings' }));
    expect(screen.queryByText('Could not load billing settings.')).toBeNull();
  });
});
