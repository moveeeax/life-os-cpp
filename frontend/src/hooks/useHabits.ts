import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { HABITS_PROBE } from '@/hooks/moduleProbes';
import { useModuleOn } from '@/hooks/useModuleProbe';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type {
  Habit,
  HabitCheck,
  HabitDays,
  HabitInput,
  MoodCheckin,
  MoodInput,
} from '@/lib/habits';

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

/**
 * Tick a habit by hand on a day, or take the tick back. A habit with a daily
 * target is ticked with the target as its value, so the day counts.
 */
export function useTickHabit(onSuccess?: () => void) {
  return useApiMutation(
    async ({ id, date, on, value }: { id: string; date: string; on: boolean; value?: number }) => {
      const path = `${BASE}/${id}/checks/${date}`;
      if (!on) return api.deleteJson<unknown>(path);
      return (
        await api.putJson<{ data: HabitCheck }>(path, {
          query: { tz: habitsZone() },
          body: value === undefined ? {} : { value },
        })
      ).data;
    },
    { invalidate: HABIT_WRITES, onSuccess },
  );
}

/** Add an amount to the day of a habit (a glass of water); the answer carries the day's sum. */
export function useAddToHabit(onSuccess?: () => void) {
  return useApiMutation(
    async ({ id, date, value }: { id: string; date: string; value: number }) =>
      (
        await api.postJson<{ data: HabitCheck }>(`${BASE}/${id}/checks/${date}/add`, {
          query: { tz: habitsZone() },
          body: { value },
        })
      ).data,
    { invalidate: HABIT_WRITES, onSuccess },
  );
}

/** The mood check-ins dated from..to, oldest first. */
export function useMood(from: string, to: string, enabled = true) {
  return useQuery({
    queryKey: qk.habits.mood(from, to),
    enabled,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: MoodCheckin[]; from: string; to: string }>(`${BASE}/mood`, {
          query: { from, to },
          signal,
        })
      ).data,
  });
}

/** Check in the mood of a day (a second check-in replaces the first), or remove it with null. */
export function useSaveMood(onSuccess?: () => void) {
  return useApiMutation(
    async ({ date, body }: { date: string; body: MoodInput | null }) => {
      const path = `${BASE}/mood/${date}`;
      if (body === null) return api.deleteJson<unknown>(path);
      return (await api.putJson<{ data: MoodCheckin }>(path, { query: { tz: habitsZone() }, body }))
        .data;
    },
    { invalidate: [qk.habits.all()], onSuccess },
  );
}

/** Whether the habits module answers (it is 404 while switched off); true until known. */
export function useHabitsEnabled(signedIn: boolean): boolean {
  return useModuleOn('habits', HABITS_PROBE, signedIn) ?? true;
}
