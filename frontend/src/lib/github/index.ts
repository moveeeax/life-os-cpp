import type { components } from '@/lib/api/schema.gen';
import { formatDay } from '@/lib/dates';

type S = components['schemas'];
export type GithubStatus = S['GithubStatus'];
export type GithubRepo = S['GithubRepo'];
export type GithubRepoList = S['GithubRepoList'];
export type GithubRule = S['GithubRule'];
export type GithubAttentionItem = S['GithubAttentionItem'];

/** The three views of the list, as the API names them. */
export type GithubView = 'attention' | 'all' | 'archived';

/** The attention rules in the order the API lists them, with the words of the sheet. */
export const RULE_LABEL: Record<GithubRule, string> = {
  checks: 'Failing checks',
  alerts: 'Dependabot alerts',
  prs: 'Open pull requests',
  issues: 'Open issues',
  hygiene: 'Description, license or README',
  stale: 'No push for a year',
};

/** The check rollup of the default branch, in words. */
export const CHECKS_LABEL: Record<GithubRepo['checks'], string> = {
  success: 'checks pass',
  failure: 'checks fail',
  pending: 'checks running',
  none: 'no checks',
};

/** The GitHub page of a repository. */
export const repoUrl = (fullName: string): string => `https://github.com/${fullName}`;

/** "public", "private fork", "archived private" and so on: what the row is at a glance. */
export function kindLine(r: Pick<GithubRepo, 'visibility' | 'fork' | 'archived'>): string {
  const words = [r.archived ? 'archived' : '', r.visibility, r.fork ? 'fork' : ''];
  return words.filter(Boolean).join(' ');
}

/** The last push as a day ("Oct 5", with the year when not this year), or "never pushed". */
export function pushedLabel(pushedAt: string | null, today: string): string {
  if (!pushedAt) return 'never pushed';
  return formatDay(pushedAt.slice(0, 10), { yearUnless: today });
}

/**
 * "synced just now", "synced 3 h ago", "synced 2 days ago": the age of the last
 * sync that worked, or "not synced yet" before the first.
 */
export function syncedAgo(syncedAt: string | null, now: Date = new Date()): string {
  if (!syncedAt) return 'not synced yet';
  const t = new Date(syncedAt).getTime();
  if (Number.isNaN(t)) return 'not synced yet';
  const min = Math.max(0, Math.round((now.getTime() - t) / 60_000));
  if (min < 2) return 'synced just now';
  if (min < 60) return `synced ${min} min ago`;
  const h = Math.round(min / 60);
  if (h < 48) return `synced ${h} h ago`;
  return `synced ${Math.round(h / 24)} days ago`;
}

/**
 * The line under an organisation that came back empty or forbidden: it may
 * restrict third-party OAuth apps, and the person approves the site's app in
 * the org settings once.
 */
export function orgHintLine(org: string | null): string | null {
  if (!org) return null;
  return `${org} may restrict third-party apps: approve Life OS in the organisation's settings once, then sync again.`;
}

/** When Sync now may be pressed again after a 429 (sync_too_soon): retry_after_sec, else a minute. */
export function syncRetryAt(body: unknown, now: Date = new Date()): number {
  const sec =
    body &&
    typeof body === 'object' &&
    typeof (body as { retry_after_sec?: unknown }).retry_after_sec === 'number'
      ? (body as { retry_after_sec: number }).retry_after_sec
      : 60;
  return now.getTime() + Math.max(1, sec) * 1000;
}

/** The query of the list: only the filters that are set, so the cache key stays short. */
export function listQuery(f: {
  view: GithubView;
  owner?: string;
  q?: string;
}): Record<string, string> {
  const out: Record<string, string> = { view: f.view };
  if (f.owner) out.owner = f.owner;
  if (f.q) out.q = f.q;
  return out;
}

/** Where a task made from a repository opens: Later, with the row opened (the site search's link). */
export const taskLink = (taskId: string): string => `/tasks/later?task=${taskId}`;
