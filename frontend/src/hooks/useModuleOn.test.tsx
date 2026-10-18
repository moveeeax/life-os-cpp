// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, renderHook, waitFor } from '@testing-library/react';
import type { ReactNode } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ApiClientError } from '@/lib/api/client';

const { get, getJson } = vi.hoisted(() => ({ get: vi.fn(), getJson: vi.fn() }));
vi.mock('@/lib/api/client', async () => {
  const actual = await vi.importActual<typeof import('@/lib/api/client')>('@/lib/api/client');
  return { ...actual, api: { GET: get, getJson } };
});

// Imported after the mock is registered.
import { useModuleOn } from './useModuleProbe';

const PROBE = [['money-enabled'], '/api/v1/money/settings'] as const;

function wrapper() {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return ({ children }: { children: ReactNode }) => (
    <QueryClientProvider client={client}>{children}</QueryClientProvider>
  );
}

afterEach(() => {
  cleanup();
  get.mockReset();
  getJson.mockReset();
});

describe('useModuleOn', () => {
  it('reads the flag from GET /api/v1/modules without a probe', async () => {
    getJson.mockResolvedValue({ data: { money: false, tasks: true } });
    const { result } = renderHook(() => useModuleOn('money', PROBE, true), { wrapper: wrapper() });
    await waitFor(() => expect(result.current).toBe(false));
    expect(getJson).toHaveBeenCalledWith('/api/v1/modules', expect.anything());
    expect(get).not.toHaveBeenCalled();
  });

  it('falls back to the probe when the modules call fails: 404 there is off', async () => {
    getJson.mockRejectedValue(new ApiClientError({ status: 500, message: 'boom' }));
    get.mockResolvedValue({
      data: undefined,
      error: new ApiClientError({ status: 404, message: 'x' }),
    });
    const { result } = renderHook(() => useModuleOn('money', PROBE, true), { wrapper: wrapper() });
    await waitFor(() => expect(result.current).toBe(false));
    expect(get).toHaveBeenCalledWith('/api/v1/money/settings', expect.anything());
  });

  it('falls back to the probe for a module the answer leaves out', async () => {
    getJson.mockResolvedValue({ data: { tasks: true } });
    get.mockResolvedValue({ data: {}, error: undefined });
    const { result } = renderHook(() => useModuleOn('money', PROBE, true), { wrapper: wrapper() });
    await waitFor(() => expect(result.current).toBe(true));
  });

  it('asks nothing while not enabled', () => {
    const { result } = renderHook(() => useModuleOn('money', PROBE, false), { wrapper: wrapper() });
    expect(result.current).toBeUndefined();
    expect(getJson).not.toHaveBeenCalled();
    expect(get).not.toHaveBeenCalled();
  });
});
