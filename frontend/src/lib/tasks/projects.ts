import type { Project } from './types';

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
