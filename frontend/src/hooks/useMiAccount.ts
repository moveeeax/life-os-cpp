import { useCallback, useEffect, useRef, useState } from 'react';
import { useQuery, useQueryClient } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { ApiClientError, api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  secondsLeft,
  type LinkState,
  type MiLinkStart,
  type MiLinkStep,
  type MiStatus,
} from '@/lib/mi';

const BASE = '/api/v1/fitness/account';

/** How often the page asks whether the user has confirmed in Xiaomi. */
export const LINK_POLL_MS = 3000;

/** The caller's Mi account link. A 404 means the fitness module is off. */
export function useMiAccount(enabled = true) {
  return useQuery({
    queryKey: qk.mi.account(),
    enabled,
    queryFn: async ({ signal }) => (await api.getJson<{ data: MiStatus }>(BASE, { signal })).data,
  });
}

const errorCode = (e: unknown): string | undefined =>
  e instanceof ApiClientError ? (e.status === 429 ? 'rate_limited' : e.code) : undefined;

/**
 * One QR link attempt of this page. `start()` asks the server for a QR; while
 * `waiting` the hook polls until the user confirms in Xiaomi, the attempt
 * fails, or it expires. Leaving the page drops the attempt.
 */
export function useMiLink() {
  const qc = useQueryClient();
  const [state, setState] = useState<LinkState>('idle');
  const [attempt, setAttempt] = useState<MiLinkStart | null>(null);
  const [error, setError] = useState<string | undefined>(undefined);
  // A start in flight: a second click must not open a second attempt.
  const starting = useRef(false);
  // Bumped by every start and cancel: an answer of an older attempt is dropped.
  const generation = useRef(0);

  const finish = useCallback((next: LinkState, code?: string) => {
    setState(next);
    setError(code);
    setAttempt(null);
  }, []);

  const start = useCallback(async () => {
    if (starting.current) return;
    starting.current = true;
    const mine = ++generation.current;
    setState('starting');
    setError(undefined);
    setAttempt(null);
    try {
      const { data } = await api.postJson<{ data: MiLinkStart }>(`${BASE}/link`);
      if (generation.current !== mine) return;
      setAttempt(data);
      setState('waiting');
    } catch (e) {
      if (generation.current === mine) finish('failed', errorCode(e));
    } finally {
      starting.current = false;
    }
  }, [finish]);

  const cancel = useCallback(() => {
    generation.current++;
    finish('idle');
  }, [finish]);

  // Poll while waiting. Each tick is one short long-poll on the server.
  useEffect(() => {
    if (state !== 'waiting' || !attempt) return;
    const mine = generation.current;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const controller = new AbortController();

    const tick = async () => {
      if (generation.current !== mine) return;
      if (secondsLeft(attempt.expires_at, Date.now()) === 0) {
        finish('expired');
        return;
      }
      try {
        const { data } = await api.getJson<{ data: MiLinkStep }>(
          `${BASE}/link/${attempt.link_id}`,
          {
            signal: controller.signal,
          },
        );
        if (generation.current !== mine) return;
        if (data.state === 'linked') {
          finish('linked');
          // The link exists now: the status, and everything that reads band data.
          void qc.invalidateQueries({ queryKey: qk.mi.account() });
          void qc.invalidateQueries({ queryKey: qk.health.all() });
          void qc.invalidateQueries({ queryKey: ['workout'] });
          return;
        }
        if (data.state === 'failed') {
          finish('failed', data.error);
          return;
        }
      } catch (e) {
        if (generation.current !== mine || controller.signal.aborted) return;
        // The server no longer has the attempt: it expired there.
        if (e instanceof ApiClientError && e.status === 404) {
          finish('expired');
          return;
        }
        // Anything else (a dropped connection): keep waiting until the deadline.
      }
      timer = setTimeout(() => void tick(), LINK_POLL_MS);
    };
    void tick();

    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [state, attempt, finish, qc]);

  // Leaving the page ends the attempt for this page.
  useEffect(
    () => () => {
      generation.current++;
    },
    [],
  );

  return { state, attempt, error, start, cancel };
}

const WORKOUT = ['workout'] as const;

export function useUnlinkMi(onDone: () => void) {
  return useApiMutation(
    (deleteData: boolean) => api.deleteJson<unknown>(BASE, { body: { delete_data: deleteData } }),
    { invalidate: [qk.mi.account(), qk.health.all(), WORKOUT], onSuccess: onDone },
  );
}

export function useSetMiRegion() {
  return useApiMutation(
    async (region: string) =>
      (await api.patchJson<{ data: MiStatus }>(BASE, { body: { region } })).data,
    { invalidate: [qk.mi.account(), qk.health.all(), WORKOUT] },
  );
}

export function useDetectMiRegion() {
  return useApiMutation(
    async () => (await api.postJson<{ data: MiStatus }>(`${BASE}/detect-region`)).data,
    { invalidate: [qk.mi.account(), qk.health.all(), WORKOUT] },
  );
}
