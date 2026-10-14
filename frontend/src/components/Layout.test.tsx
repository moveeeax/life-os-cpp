// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter, Route, Routes } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { ToastProvider } from '@/components/ui/toaster';

import { Layout } from './Layout';

/**
 * Hide numbers on the classic layout: the toggle renders in the nav, flips
 * the `hide-numbers` class on <html>, and the wallet balance wears `.num`
 * so the global rule (index.css) blurs it.
 */

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
  useWalletSummary: () => ({ isSuccess: true, data: { data: { balance: 1234 } } }),
}));
vi.mock('@/hooks/useRegistration', () => ({ useRegistrationOpen: () => false }));
vi.mock('@/hooks/useAuthMutations', () => ({
  useLogout: () => ({ mutateAsync: vi.fn(), isPending: false }),
}));

function renderLayout() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={['/billing']}>
          <Routes>
            <Route element={<Layout />}>
              <Route path="/billing" element={<p>wallet page</p>} />
            </Route>
          </Routes>
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  document.documentElement.classList.remove('hide-numbers');
  try {
    localStorage.clear();
  } catch {
    // jsdom storage is there; a blocked one changes nothing here.
  }
});

describe('Layout (classic pages)', () => {
  it('renders the Hide numbers toggle and flips the class on <html>', () => {
    renderLayout();
    const toggle = screen.getByRole('button', { name: 'Hide numbers' });
    expect(toggle.getAttribute('aria-pressed')).toBe('false');
    expect(document.documentElement.classList.contains('hide-numbers')).toBe(false);

    fireEvent.click(toggle);
    expect(document.documentElement.classList.contains('hide-numbers')).toBe(true);
    const pressed = screen.getByRole('button', { name: 'Show numbers' });
    expect(pressed.getAttribute('aria-pressed')).toBe('true');
    expect(localStorage.getItem('hide-numbers')).toBe('1');

    fireEvent.click(pressed);
    expect(document.documentElement.classList.contains('hide-numbers')).toBe(false);
  });

  it('marks the wallet balance in the nav as a number', () => {
    renderLayout();
    const balances = screen.getAllByText('1,234');
    expect(balances.length).toBeGreaterThan(0);
    for (const b of balances) expect(b.classList.contains('num')).toBe(true);
  });

  it('the blur rule is global, not scoped to the glass shell', () => {
    // A CSS engine is not in the test stack; the stylesheet text is.
    // vitest runs from frontend/ (the package root).
    const css = (p: string) => readFileSync(resolve(process.cwd(), p), 'utf8');
    expect(css('src/index.css')).toMatch(/\.hide-numbers \.num \{/);
    expect(css('src/shell/glass.css')).not.toMatch(/\.hide-numbers \.glass \.num\b/);
  });
});
