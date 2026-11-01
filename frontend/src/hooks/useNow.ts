import { useQuery, useQueryClient } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { NOW_RIBBON, qk } from '@/lib/api/queryKeys';
import {
  cacheKey,
  pruneDays,
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
const AFTER_FOCUS = [...NOW_RIBBON, qk.tasks.all()] as const;
/** An admin wallpaper write moves the wallpaper lists; a sound write the sound lists. */
const WALLPAPER_WRITES = [qk.now.wallpaperLists(), qk.now.adminWallpapers()] as const;
const SOUND_WRITES = [qk.now.sounds(), qk.now.adminSounds()] as const;

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
      if (key && userId) {
        writeCache(key, data);
        pruneDays(userId, date);
      }
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

// ── settings and weather ───────────────────────────────────────────────────

/**
 * The Now settings of a user. The last answer paints first, so a moved or
 * hidden corner does not flash in the default layout on a cold load.
 */
export function useNowSettings(userId: string | null) {
  const key = userId ? `now:settings:${userId}` : null;
  return useQuery({
    queryKey: qk.now.settings(),
    enabled: userId !== null,
    initialData: () => (key ? (readCache<NowSettings>(key) ?? undefined) : undefined),
    initialDataUpdatedAt: 0,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) => {
      const data = (await api.getJson<{ data: NowSettings }>('/api/v1/now/settings', { signal }))
        .data;
      if (key) writeCache(key, data);
      return data;
    },
  });
}

export function useSaveNowSettings(onSuccess?: () => void) {
  return useApiMutation(
    async (body: NowSettingsInput) =>
      (await api.putJson<{ data: NowSettings }>('/api/v1/now/settings', { body })).data,
    { invalidate: [qk.now.settings(), qk.now.weather()], onSuccess },
  );
}

/**
 * Save a new dock order. The order shows at once; the writes go one after
 * another and the last answer is the one kept, so a second drag before the
 * first answer neither snaps the tiles back nor loses either move.
 */
export function useSaveDockOrder(userId: string | null) {
  const qc = useQueryClient();
  const key = userId ? `now:settings:${userId}` : null;
  const save = useApiMutation(
    async (corners: NowSettings['corners']) =>
      (await api.putJson<{ data: NowSettings }>('/api/v1/now/settings', { body: { corners } }))
        .data,
    {
      scope: { id: 'now-settings' },
      onSuccess: (data) => {
        // A later move is still queued: its answer will be the one to keep.
        if (qc.isMutating({ predicate: (m) => m.options.scope?.id === 'now-settings' }) > 1) return;
        qc.setQueryData(qk.now.settings(), data);
        if (key) writeCache(key, data);
      },
      onError: () => void qc.invalidateQueries({ queryKey: qk.now.settings() }),
    },
  );
  return (corners: NowSettings['corners']) => {
    qc.setQueryData<NowSettings>(qk.now.settings(), (old) => (old ? { ...old, corners } : old));
    save.mutate(corners);
  };
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
    { invalidate: WALLPAPER_WRITES },
  );
}

export function useDeleteWallpaper() {
  return useApiMutation((id: string) => del(`${ADMIN}/${id}`), {
    invalidate: WALLPAPER_WRITES,
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
    { invalidate: SOUND_WRITES },
  );
}

export function useDeleteSound() {
  return useApiMutation((id: string) => del(`${SOUNDS_ADMIN}/${id}`), {
    invalidate: SOUND_WRITES,
  });
}
