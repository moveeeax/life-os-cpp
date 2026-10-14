// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { MemoryRouter, Route, Routes, useLocation } from 'react-router';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import { LoginRedirect } from '@/pages/Root';
import { SignInPage } from '@/pages/SignIn';

import { ProtectedRoute } from './ProtectedRoute';

/**
 * The redirect-after-login round trip, through the real router: a guest on
 * /money?row=abc#top is bounced to /login, which lands on the sign-in form
 * at /; after the login the form navigates back to the FULL location, with
 * its query string and hash, and the guard lets the page through.
 */

const user = { id: 'u1', email: 'me@example.com', confirmed: true, role: { permissions: 0 } };
const session = vi.hoisted(() => ({ current: null as unknown }));

vi.mock('@/hooks/useMe', () => ({
  useMe: () => ({
    data: session.current,
    isPending: false,
    isError: false,
    isSuccess: true,
    isFetching: false,
    refetch: vi.fn(),
  }),
}));
const login = vi.hoisted(() => ({ mutateAsync: vi.fn() }));
vi.mock('@/hooks/useAuthMutations', () => ({
  useLogin: () => ({ mutateAsync: login.mutateAsync, isPending: false }),
}));

function Where() {
  const l = useLocation();
  return <p data-testid="where">{l.pathname + l.search + l.hash}</p>;
}

function renderApp(initial: string) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={[initial]}>
          <Routes>
            <Route path="/" element={<SignInPage />} />
            <Route path="/login" element={<LoginRedirect />} />
            <Route
              path="/money"
              element={
                <ProtectedRoute requireConfirmed>
                  <Where />
                </ProtectedRoute>
              }
            />
          </Routes>
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

beforeEach(() => {
  session.current = null;
  login.mutateAsync.mockReset();
});

afterEach(() => {
  cleanup();
});

describe('ProtectedRoute → SignIn → back', () => {
  it('returns to the full location, query string and hash included', async () => {
    renderApp('/money?row=abc&kind=week#top');
    // A guest sees the sign-in form (via /login → /).
    const email = await screen.findByLabelText('Email');
    expect(screen.queryByTestId('where')).toBeNull();

    login.mutateAsync.mockImplementation(async () => {
      session.current = user;
    });
    fireEvent.change(email, { target: { value: 'me@example.com' } });
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'hunter2' } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    await waitFor(() =>
      expect(screen.getByTestId('where').textContent).toBe('/money?row=abc&kind=week#top'),
    );
    expect(login.mutateAsync).toHaveBeenCalledWith({
      email: 'me@example.com',
      password: 'hunter2',
    });
  });

  it('a signed-in user passes straight through', () => {
    session.current = user;
    renderApp('/money?row=abc');
    expect(screen.getByTestId('where').textContent).toBe('/money?row=abc');
  });
});
