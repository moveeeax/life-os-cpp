// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';

import { GlassAccountMenu } from './GlassAccountMenu';

const user = {
  id: 'u1',
  email: 'me@example.com',
  full_name: 'Me',
  confirmed: true,
  role_id: 1,
  role: { id: 1, name: 'user', permissions: 0 },
};

vi.mock('@/hooks/useMe', () => ({
  useMe: () => ({ data: user, isSuccess: true, isPending: false, isError: false }),
}));
vi.mock('@/hooks/useWallet', () => ({
  useWalletSummary: () => ({ isSuccess: false }),
}));
vi.mock('@/hooks/useAuthMutations', () => ({
  useLogout: () => ({ mutateAsync: vi.fn(), isPending: false }),
}));
vi.mock('@/hooks/useNow', () => ({
  useNowSettings: () => ({ data: undefined }),
}));

function mount(path = '/tasks') {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={[path]}>
          <GlassAccountMenu />
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
  fireEvent.click(screen.getByRole('button', { name: /^Account:/ }));
}

afterEach(() => {
  cleanup();
  document.documentElement.classList.remove('hide-numbers');
  try {
    localStorage.removeItem('hide-numbers');
  } catch {
    // jsdom without storage
  }
});

describe('Hide numbers in the account menu', () => {
  it('toggles the class on the root and relabels itself', () => {
    mount();
    const row = screen.getByRole('button', { name: 'Hide numbers' });
    fireEvent.click(row);
    expect(document.documentElement.classList.contains('hide-numbers')).toBe(true);
    expect(screen.getByRole('button', { name: 'Show numbers' })).toBeTruthy();
  });
});

describe('Ani in the account menu', () => {
  afterEach(() => {
    try {
      localStorage.removeItem('now:ani');
    } catch {
      // jsdom without storage
    }
  });

  it('is offered on Now only and turns her on and off', () => {
    mount();
    expect(screen.queryByRole('button', { name: 'Show Ani' })).toBeNull();
    cleanup();
    mount('/now');
    fireEvent.click(screen.getByRole('button', { name: 'Show Ani' }));
    expect(localStorage.getItem('now:ani')).toBe('1');
    fireEvent.click(screen.getByRole('button', { name: /^Account:/ }));
    fireEvent.click(screen.getByRole('button', { name: 'Hide Ani' }));
    expect(localStorage.getItem('now:ani')).toBe('0');
  });
});
