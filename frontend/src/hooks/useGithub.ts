import { useInfiniteQuery, useQuery, useQueryClient } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { GITHUB_PROBE } from '@/hooks/moduleProbes';
import { useModuleOn } from '@/hooks/useModuleProbe';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  listQuery,
  type GithubRepo,
  type GithubRepoList,
  type GithubRule,
  type GithubStatus,
  type GithubView,
} from '@/lib/github';

const BASE = '/api/v1/github';
const PAGE = 50;

/** A mute or a sync moves the lists and the counts; a task made from a repository moves Tasks and Now too. */
const WRITES = [qk.github.all()] as const;
const TASK_WRITES = [qk.github.all(), qk.tasks.all(), qk.now.all()] as const;

/** Whether the GitHub module is on (undefined while asked). */
export function useGithubOn(enabled = true): boolean | undefined {
  return useModuleOn('github', GITHUB_PROBE, enabled);
}

/** GET /github: whether the server has an OAuth App and whether the caller linked. */
export function useGithubStatus(enabled = true) {
  return useQuery({
    queryKey: qk.github.status(),
    enabled,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: GithubStatus }>(BASE, { signal })).data,
  });
}

export interface RepoFilter {
  view: GithubView;
  owner?: string;
  q?: string;
}

/** The rows of one view, fifty at a time; the counts and the owners ride on every page. */
export function useGithubRepos(filter: RepoFilter, enabled = true) {
  const query = listQuery(filter);
  return useInfiniteQuery({
    queryKey: qk.github.repos(JSON.stringify(query)),
    enabled,
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) =>
      api.getJson<GithubRepoList>(`${BASE}/repos`, {
        query: { ...query, limit: PAGE, offset: pageParam },
        signal,
      }),
    getNextPageParam: (last, pages) => {
      const loaded = pages.reduce((n, p) => n + p.data.length, 0);
      return last.data.length > 0 && loaded < last.total ? loaded : undefined;
    },
  });
}

/** One repository; the row of the list seeds it so the sheet opens at once. */
export function useGithubRepo(fullName: string, seed?: GithubRepo) {
  return useQuery({
    queryKey: qk.github.repo(fullName),
    initialData: seed,
    initialDataUpdatedAt: seed ? Date.parse(seed.synced_at) || Date.now() : undefined,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: GithubRepo }>(`${BASE}/repos/${fullName}`, { signal })).data,
  });
}

/** GET /github/oauth/start is a redirect: the browser goes there itself. */
export const OAUTH_START = `${BASE}/oauth/start`;

export function useGithubUnlink(onSuccess?: () => void) {
  return useApiMutation(async () => await api.deleteJson<unknown>(BASE), {
    invalidate: WRITES,
    onSuccess,
  });
}

/** POST /github/sync: queues a sync; 429 sync_too_soon when one ran within a minute. */
export function useGithubSync() {
  return useApiMutation(async () => await api.postJson<unknown>(`${BASE}/sync`), {
    invalidate: [qk.github.status()],
  });
}

/** PUT (silence) or DELETE (let it speak) a rule of a repository; the answer is the repository as it reads now. */
export function useGithubMute(fullName: string) {
  const qc = useQueryClient();
  return useApiMutation(
    async ({ rule, muted }: { rule: GithubRule; muted: boolean }) => {
      const path = `${BASE}/repos/${fullName}/mutes/${rule}`;
      return (
        await (muted
          ? api.putJson<{ data: GithubRepo }>(path)
          : api.deleteJson<{ data: GithubRepo }>(path))
      ).data;
    },
    {
      invalidate: WRITES,
      onSuccess: (repo) => qc.setQueryData(qk.github.repo(fullName), repo),
    },
  );
}

/** POST /github/repos/{owner}/{name}/task: one task per repository and rule; created says whether this call made it. */
export function useGithubTask(fullName: string) {
  return useApiMutation(
    async (rule: GithubRule) =>
      (
        await api.postJson<{ data: { task_id: string; created: boolean } }>(
          `${BASE}/repos/${fullName}/task`,
          { body: { rule } },
        )
      ).data,
    { invalidate: TASK_WRITES },
  );
}
