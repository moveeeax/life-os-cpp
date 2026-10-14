import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  cacheKey,
  readCache,
  writeCache,
  type AdminSound,
  type AdminWallpaper,
  type Device,
  type Focus,
  type Sound,
  type SoundCreate,
  type FocusInput,
  type NowSettings,
  type NowSettingsInput,
  type NowWeather,
  type Today,
  type Wallpaper,
  type WallpaperCreate,
  type WallpaperUpload,
} from '@/lib/now';

const TODAY = '/api/v1/today';
const ADMIN = '/api/v1/admin/wallpapers';

/** A DELETE answering 204: no body to parse, only an error to raise. */
async function del(path: string, query?: Record<string, string>): Promise<void> {
  const { error } = await api.DELETE(path, query ? { query } : undefined);
  if (error) throw error;
}

/** The focus moves a task, so a focus write refreshes Tasks too. */
const AFTER_FOCUS = [qk.now.all(), qk.tasks.all()] as const;

/**
 * Whether the now module answers at all: the wallpaper list is 404 only while
 * the module is off (503 means on, without storage). Undefined while asking.
 */
export function useNowEnabled(signedIn: boolean): boolean | undefined {
  const probe = useQuery({
    queryKey: qk.now.enabled(),
    enabled: signedIn,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET('/api/v1/wallpapers?device=desktop' as string, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data;
}

/**
 * The day's digest. The last answer for this user and day paints first
 * (a new tab opens at once); the server answer replaces it and is cached.
 */
export function useToday(userId: string | null, date: string, tz: string) {
  const key = userId ? cacheKey(userId, date) : null;
  return useQuery({
    queryKey: qk.now.today(date, tz),
    enabled: userId !== null,
    initialData: () => (key ? (readCache<Today>(key) ?? undefined) : undefined),
    initialDataUpdatedAt: 0,
    staleTime: 60_000,
    queryFn: async ({ signal }) => {
      const data = (await api.getJson<{ data: Today }>(TODAY, { query: { date, tz }, signal }))
        .data;
      if (key) writeCache(key, data);
      return data;
    },
  });
}

/** The ready wallpapers of a device; the last list paints first. */
export function useWallpapers(device: Device, enabled = true) {
  const key = `now:wallpapers:${device}`;
  return useQuery({
    queryKey: qk.now.wallpapers(device),
    enabled,
    initialData: () => readCache<Wallpaper[]>(key) ?? undefined,
    initialDataUpdatedAt: 0,
    staleTime: 10 * 60_000,
    retry: false,
    queryFn: async ({ signal }) => {
      const data = (
        await api.getJson<{ data: Wallpaper[] }>('/api/v1/wallpapers', {
          query: { device },
          signal,
        })
      ).data;
      writeCache(key, data);
      return data;
    },
  });
}

export function useSetFocus(onSuccess?: () => void) {
  return useApiMutation(
    async (body: FocusInput) =>
      (await api.putJson<{ data: Focus }>(`${TODAY}/focus`, { body })).data,
    { invalidate: AFTER_FOCUS, onSuccess },
  );
}

export function useFocusDone() {
  return useApiMutation(
    async (body: { date: string; done: boolean }) =>
      (await api.postJson<{ data: Focus }>(`${TODAY}/focus/done`, { body })).data,
    { invalidate: AFTER_FOCUS },
  );
}

// ── the note, settings and weather ─────────────────────────────────────────

/** Saves the note of a day; an empty text clears it. */
export function useSaveNote() {
  return useApiMutation(
    async ({ date, text }: { date: string; text: string }) => {
      if (text.trim() === '') return del(`${TODAY}/note`, { date });
      await api.putJson<unknown>(`${TODAY}/note`, { body: { date, text: text.trim() } });
    },
    // The day's digest only: settings, weather and wallpapers do not change with a note.
    { invalidate: [qk.now.days()] },
  );
}

export function useNowSettings(enabled = true) {
  return useQuery({
    queryKey: qk.now.settings(),
    enabled,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: NowSettings }>('/api/v1/now/settings', { signal })).data,
  });
}

export function useSaveNowSettings(onSuccess?: () => void) {
  return useApiMutation(
    async (body: NowSettingsInput) =>
      (await api.putJson<{ data: NowSettings }>('/api/v1/now/settings', { body })).data,
    { invalidate: [qk.now.settings(), qk.now.weather()], onSuccess },
  );
}

/** The weather of the settings city; null without one. Quiet on failure: Now paints without it. */
export function useWeather(enabled: boolean) {
  return useQuery({
    queryKey: qk.now.weather(),
    enabled,
    staleTime: 15 * 60_000,
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: NowWeather | null }>('/api/v1/now/weather', { signal })).data,
  });
}

// ── admin ───────────────────────────────────────────────────────────────────

export function useAdminWallpapers() {
  return useQuery({
    queryKey: qk.now.adminWallpapers(),
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: AdminWallpaper[] }>(ADMIN, { signal })).data,
  });
}

export interface WallpaperCreated {
  id: string;
  upload: WallpaperUpload;
  poster_upload: WallpaperUpload | null;
}

export function useCreateWallpaper() {
  return useApiMutation(
    async (body: WallpaperCreate) =>
      (await api.postJson<{ data: WallpaperCreated }>(ADMIN, { body })).data,
  );
}

export function useWallpaperReady() {
  return useApiMutation(
    async (id: string) => (await api.postJson<{ data: Wallpaper }>(`${ADMIN}/${id}/ready`)).data,
    { invalidate: [qk.now.all()] },
  );
}

export function useDeleteWallpaper() {
  return useApiMutation((id: string) => del(`${ADMIN}/${id}`), {
    invalidate: [qk.now.all()],
  });
}

// ── sounds ──────────────────────────────────────────────────────────────────

const SOUNDS_ADMIN = '/api/v1/admin/sounds';

/** The ready focus sounds; empty while storage is not set up. */
export function useSounds(enabled = true) {
  return useQuery({
    queryKey: qk.now.sounds(),
    enabled,
    retry: false,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Sound[] }>('/api/v1/sounds', { signal })).data,
  });
}

export function useAdminSounds() {
  return useQuery({
    queryKey: qk.now.adminSounds(),
    retry: false,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: AdminSound[] }>(SOUNDS_ADMIN, { signal })).data,
  });
}

export function useCreateSound() {
  return useApiMutation(
    async (body: SoundCreate) =>
      (
        await api.postJson<{ data: { id: string; upload: WallpaperUpload } }>(SOUNDS_ADMIN, {
          body,
        })
      ).data,
  );
}

export function useSoundReady() {
  return useApiMutation(
    async (id: string) => (await api.postJson<{ data: Sound }>(`${SOUNDS_ADMIN}/${id}/ready`)).data,
    { invalidate: [qk.now.all()] },
  );
}

export function useDeleteSound() {
  return useApiMutation((id: string) => del(`${SOUNDS_ADMIN}/${id}`), {
    invalidate: [qk.now.all()],
  });
}
