import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  cacheKey,
  readCache,
  writeCache,
  type AdminWallpaper,
  type Device,
  type Focus,
  type FocusInput,
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

export function useClearFocus(onSuccess?: () => void) {
  return useApiMutation((date: string) => del(`${TODAY}/focus`, { date }), {
    invalidate: AFTER_FOCUS,
    onSuccess,
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
