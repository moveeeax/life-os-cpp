import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Project, ProjectInput, Task } from '@/lib/tasks';

const BASE = '/api/v1/tasks/projects';

/** A project write changes task rows (their project name) and the counts. */
const WRITES = [qk.tasks.all(), qk.now.all()] as const;

/** Projects of a kind ("" both), in their order. */
export function useProjects(kind: '' | 'project' | 'course' = '') {
  return useQuery({
    queryKey: qk.tasks.projects(kind),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Project[] }>(BASE, {
          query: kind ? { kind } : undefined,
          signal,
        })
      ).data,
  });
}

export function useProject(id: string) {
  return useQuery({
    queryKey: qk.tasks.project(id),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Project }>(`${BASE}/${id}`, { signal })).data,
  });
}

/** The tasks of one project or course, open and done. */
export function useProjectTasks(id: string) {
  return useQuery({
    queryKey: qk.tasks.byProject(id),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Task[] }>('/api/v1/tasks/items', {
          query: { project: id, limit: 200 },
          signal,
        })
      ).data,
  });
}

export function useSaveProject(id: string | null, onSuccess?: (p: Project) => void) {
  return useApiMutation(
    async (body: ProjectInput) =>
      id
        ? (await api.patchJson<{ data: Project }>(`${BASE}/${id}`, { body })).data
        : (await api.postJson<{ data: Project }>(BASE, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

export function useDeleteProject(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}
