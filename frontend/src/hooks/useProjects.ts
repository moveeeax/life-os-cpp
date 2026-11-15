import { useQuery } from '@tanstack/react-query';

import { useDeleteEntity, useSaveEntity } from '@/hooks/crud';
import { api } from '@/lib/api/client';
import { NOW_RIBBON, qk } from '@/lib/api/queryKeys';
import { loadProjectTasks, type Project, type ProjectInput, type Task } from '@/lib/tasks';

const BASE = '/api/v1/tasks/projects';

/** A project write changes task and note rows (their project name) and the counts. */
const WRITES = [qk.tasks.all(), ...NOW_RIBBON, qk.notes.all()] as const;

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

/** The tasks of one project or course: every open one, the newest done ones. */
export function useProjectTasks(id: string) {
  return useQuery({
    queryKey: qk.tasks.byProject(id),
    queryFn: ({ signal }) =>
      loadProjectTasks((status, limit, offset) =>
        api.getJson<{ data: Task[]; total: number }>('/api/v1/tasks/items', {
          query: { project: id, status, limit, offset },
          signal,
        }),
      ),
  });
}

export function useSaveProject(id: string | null, onSuccess?: (p: Project) => void) {
  return useSaveEntity<ProjectInput, Project>(BASE, id, { invalidate: WRITES, onSuccess });
}

export function useDeleteProject(onSuccess?: () => void) {
  return useDeleteEntity(BASE, { invalidate: WRITES, onSuccess });
}
