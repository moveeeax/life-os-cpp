// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, renderHook } from '@testing-library/react';
import type { ReactNode } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';

import { useMuteKind, type AniPrefs } from './useAni';

/** "Not about this": each mute adds to the ones already kept, even before the prefs refetch. */

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('useMuteKind', () => {
  it('MutesAddUp: a second mute keeps the first', async () => {
    const put = vi.spyOn(api, 'putJson').mockResolvedValue({ data: {} });
    vi.spyOn(api, 'getJson').mockResolvedValue({ data: { bare: false, muted: ['hello'] } });
    const qc = new QueryClient({
      defaultOptions: { queries: { retry: false, staleTime: 60_000 } },
    });
    qc.setQueryData(qk.ani.prefs(), { bare: false, muted: ['hello'] } as AniPrefs);
    const wrapper = ({ children }: { children: ReactNode }) => (
      <QueryClientProvider client={qc}>{children}</QueryClientProvider>
    );
    const { result } = renderHook(() => useMuteKind(), { wrapper });
    await act(async () => {
      result.current('brief');
      result.current('evening');
    });
    expect(put).toHaveBeenLastCalledWith('/api/v1/ani/prefs', {
      body: { muted: ['hello', 'brief', 'evening'] },
    });
    expect(qc.getQueryData<AniPrefs>(qk.ani.prefs())?.muted).toEqual(['hello', 'brief', 'evening']);
  });
});
