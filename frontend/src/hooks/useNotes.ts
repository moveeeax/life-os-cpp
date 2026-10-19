import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { NOTES_PROBE } from '@/hooks/moduleProbes';
import { useModuleOn } from '@/hooks/useModuleProbe';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Note, NoteFacet, NoteInput } from '@/lib/notes';

const BASE = '/api/v1/notes';

/** A note write changes the lists, a project's notes and a course's notes. */
const WRITES = [qk.notes.all()] as const;

export interface NoteFilter {
  q?: string;
  topic?: string;
  tag?: string;
  project?: string;
}

export interface NoteList {
  data: Note[];
  total: number;
  topics: NoteFacet[];
  tags: NoteFacet[];
}

/** Whether the notes module answers at all (404 while it is off). */
export function useNotesEnabled(signedIn: boolean): boolean {
  return useModuleOn('notes', NOTES_PROBE, signedIn) ?? true;
}

export function useNoteList(filter: NoteFilter, enabled = true) {
  const query: Record<string, string | number> = { limit: 200 };
  for (const [k, v] of Object.entries(filter)) if (v) query[k] = v;
  return useQuery({
    queryKey: qk.notes.list(JSON.stringify(query)),
    enabled,
    queryFn: async ({ signal }) => await api.getJson<NoteList>(BASE, { query, signal }),
  });
}

export function useNote(id: string) {
  return useQuery({
    queryKey: qk.notes.one(id),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Note }>(`${BASE}/${id}`, { signal })).data,
  });
}

export function useSaveNote(id: string | null, onSuccess?: (n: Note) => void) {
  return useApiMutation(
    async (body: NoteInput) =>
      id
        ? (await api.patchJson<{ data: Note }>(`${BASE}/${id}`, { body })).data
        : (await api.postJson<{ data: Note }>(BASE, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

export function useDeleteNote(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}
