import type { Project, Task } from './types';

export const PROJECT_STATUS: Record<Project['status'], string> = {
  planned: 'Planned',
  active: 'Active',
  paused: 'Paused',
  done: 'Done',
};

/**
 * The project a phrase names with "#code", and the phrase without that code.
 * An unknown "#tag" (an issue number, a hashtag) stays in the text.
 */
export function takeProjectCode(
  text: string,
  projects: { id: string; code: string }[],
): { text: string; projectId: string | null } {
  let projectId: string | null = null;
  const out = text.replace(
    /(^|\s)#([a-z0-9-]{1,24})(?=\s|$)/gi,
    (whole, lead: string, code: string) => {
      const p = projects.find((x) => x.code === code.toLowerCase());
      if (!p || projectId) return whole;
      projectId = p.id;
      return lead;
    },
  );
  const rest = out.replace(/\s{2,}/g, ' ').trim();
  // A phrase that is only a code stays the title as typed, with no project.
  return rest ? { text: rest, projectId } : { text: text.trim(), projectId: null };
}

/** Done steps of all the steps of a course; null when it has none yet. */
export function courseProgress(p: {
  open: number;
  done: number;
}): { done: number; total: number; share: number } | null {
  const total = p.open + p.done;
  return total === 0 ? null : { done: p.done, total, share: p.done / total };
}

/** One page of a project's tasks in one status, as GET /tasks/items answers. */
export type TaskPageFetch = (
  status: Task['status'],
  limit: number,
  offset: number,
) => Promise<{ data: Task[]; total: number }>;

/** The API's largest page (kMaxLimit). */
export const TASK_PAGE = 200;
const MAX_PAGES = 25;

/**
 * Every open and in-progress task of a project, paged through, plus the newest
 * page of done ones, newest first. One mixed call of 200 dropped the oldest
 * open tasks once a project passed 200 tasks, while the header still counted them.
 */
export async function loadProjectTasks(fetchPage: TaskPageFetch): Promise<Task[]> {
  const all = async (status: Task['status']) => {
    const out: Task[] = [];
    for (let i = 0; i < MAX_PAGES; i++) {
      const page = await fetchPage(status, TASK_PAGE, out.length);
      out.push(...page.data);
      if (page.data.length < TASK_PAGE || out.length >= page.total) break;
    }
    return out;
  };
  const [open, going, done] = await Promise.all([
    all('open'),
    all('in_progress'),
    fetchPage('done', TASK_PAGE, 0).then((p) => p.data),
  ]);
  return [...open, ...going, ...done].sort((a, b) =>
    a.created_at < b.created_at ? 1 : a.created_at > b.created_at ? -1 : 0,
  );
}
