import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { useModuleProbe } from '@/hooks/useModuleProbe';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Habit, HabitCheck, HabitDays, HabitInput } from '@/lib/habits';

const BASE = '/api/v1/habits';

/** The caller's zone: today and the local day of a Training session are counted in it. */
export function habitsZone(): string {
  return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
}

/** A habit write moves the section and Now's digest (the Habits plate). */
export const HABIT_WRITES = [qk.habits.all(), qk.now.all()] as const;

export function useHabits(archived = false) {
  return useQuery({
    queryKey: qk.habits.list(archived),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Habit[] }>(BASE, {
          query: { archived: archived ? 'true' : 'false' },
          signal,
        })
      ).data,
  });
}

export interface HabitDaysAnswer {
  data: HabitDays[];
  from: string;
  to: string;
  today: string;
}

/** The active habits over [from, to] with their streaks, in the caller's zone. */
export function useHabitDays(from: string, to: string, enabled = true) {
  const tz = habitsZone();
  return useQuery({
    queryKey: qk.habits.days(from, to, tz),
    enabled,
    queryFn: async ({ signal }) =>
      await api.getJson<HabitDaysAnswer>(`${BASE}/days`, { query: { from, to, tz }, signal }),
  });
}

export function useSaveHabit(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: HabitInput) =>
      id
        ? (await api.patchJson<{ data: Habit }>(`${BASE}/${id}`, { body })).data
        : (await api.postJson<{ data: Habit }>(BASE, { body })).data,
    { invalidate: HABIT_WRITES, onSuccess },
  );
}

export function useArchiveHabit(onSuccess?: () => void) {
  return useApiMutation(
    async ({ id, archived }: { id: string; archived: boolean }) =>
      (await api.postJson<{ data: Habit }>(`${BASE}/${id}/archive`, { body: { archived } })).data,
    { invalidate: HABIT_WRITES, onSuccess },
  );
}

export function useDeleteHabit(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: HABIT_WRITES,
    onSuccess,
  });
}

/** Tick a habit by hand on a day, or take the tick back. */
export function useTickHabit(onSuccess?: () => void) {
  return useApiMutation(
    async ({ id, date, on }: { id: string; date: string; on: boolean }) => {
      const path = `${BASE}/${id}/checks/${date}`;
      if (!on) return api.deleteJson<unknown>(path);
      return (
        await api.putJson<{ data: HabitCheck }>(path, { query: { tz: habitsZone() }, body: {} })
      ).data;
    },
    { invalidate: HABIT_WRITES, onSuccess },
  );
}

/** [query key, url] of the probe that says whether the module is on. */
export const HABITS_PROBE = [qk.habits.enabled(), BASE] as const;

/** Whether the habits module answers (it is 404 while switched off); true until known. */
export function useHabitsEnabled(signedIn: boolean): boolean {
  return useModuleProbe(...HABITS_PROBE, signedIn) ?? true;
}
