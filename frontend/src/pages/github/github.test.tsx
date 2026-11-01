// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { MemoryRouter, Route, Routes } from 'react-router';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ToastProvider } from '@/components/ui/toaster';
import { api } from '@/lib/api/client';
import type { GithubRepo, GithubRepoList, GithubStatus } from '@/lib/github';

import { GithubPage } from './List';

/**
 * The GitHub section: the tabs and their counts, the owner chips, the search,
 * the sheet with its actions, and the Hide numbers guard over all of it.
 */

const status: GithubStatus = {
  configured: true,
  linked: true,
  login: 'moveeeax',
  scopes: ['repo', 'read:org'],
  linked_at: '2026-11-01T08:00:00Z',
  synced_at: '2026-11-01T09:00:00Z',
  sync_error: null,
  org_hint: null,
};

const repo = (over: Partial<GithubRepo>): GithubRepo => ({
  full_name: 'moveeeax/life-os-cpp',
  owner: 'moveeeax',
  name: 'life-os-cpp',
  visibility: 'public',
  fork: false,
  archived: false,
  description: 'Life OS, the site: 3 services',
  topics: ['cpp', 'k8s'],
  license: 'MIT',
  stars: 42,
  pushed_at: '2026-10-30T10:00:00Z',
  default_branch: 'master',
  open_issues: 3,
  open_prs: 1,
  open_alerts: 0,
  checks: 'failure',
  has_readme: true,
  attention: [
    { rule: 'checks', detail: 'checks fail on master' },
    { rule: 'issues', detail: '3 open issues' },
  ],
  muted: ['prs'],
  tasks: { checks: 't-checks' },
  synced_at: '2026-11-01T09:00:00Z',
  ...over,
});

const rows: GithubRepo[] = [
  repo({}),
  repo({
    full_name: 'cybercapybara/site',
    owner: 'cybercapybara',
    name: 'site',
    visibility: 'private',
    stars: 0,
    attention: [],
    muted: [],
    tasks: {},
    pushed_at: '2024-02-10T10:00:00Z',
  }),
];

const list = (data: GithubRepo[], total = data.length): GithubRepoList => ({
  data,
  total,
  limit: 50,
  offset: 0,
  owners: ['cybercapybara', 'moveeeax'],
  counts: { attention: 12, all: 378, archived: 4 },
});

function mockApi(opts: { status?: GithubStatus; rows?: GithubRepo[]; total?: number } = {}) {
  const get = vi.spyOn(api, 'getJson').mockImplementation(async (path: string) => {
    if (path === '/api/v1/github') return { data: opts.status ?? status };
    if (path === '/api/v1/github/repos') return list(opts.rows ?? rows, opts.total);
    const one = (opts.rows ?? rows).find((r) => path.endsWith(`/repos/${r.full_name}`));
    if (one) return { data: one };
    throw new Error(`unexpected GET ${path}`);
  });
  const post = vi
    .spyOn(api, 'postJson')
    .mockResolvedValue({ data: { task_id: 't-new', created: true } });
  const put = vi
    .spyOn(api, 'putJson')
    .mockResolvedValue({ data: repo({ attention: [], muted: ['prs', 'checks', 'issues'] }) });
  const del = vi.spyOn(api, 'deleteJson').mockResolvedValue({ data: repo({ muted: [] }) });
  return { get, post, put, del };
}

function mount(view: 'attention' | 'all' | 'archived' = 'attention') {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <ToastProvider>
        <MemoryRouter initialEntries={['/github']}>
          <Routes>
            <Route path="/github" element={<GithubPage view={view} />} />
            <Route path="/tasks/later" element={<div data-testid="later" />} />
          </Routes>
        </MemoryRouter>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

const MARKED = '.num, [data-num-ok]';

/** Every text with a digit that neither blurs nor says it is not personal. */
function unmarked(root: HTMLElement): string[] {
  const out: string[] = [];
  const walk = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  for (let n = walk.nextNode(); n; n = walk.nextNode()) {
    const text = n.textContent ?? '';
    if (!/\d/.test(text)) continue;
    const parent = n.parentElement;
    if (!parent || parent.closest('option, script, style')) continue;
    if (!parent.closest(MARKED)) out.push(`${parent.tagName.toLowerCase()}: ${text.trim()}`);
  }
  return out;
}

/** The query of the last GET /github/repos. */
function lastListQuery(get: ReturnType<typeof mockApi>['get']): Record<string, unknown> {
  const calls = get.mock.calls.filter((c) => c[0] === '/api/v1/github/repos');
  const opts = calls[calls.length - 1]?.[1] as { query?: Record<string, unknown> } | undefined;
  return opts?.query ?? {};
}

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('GitHub list', () => {
  it('asks to link on Account when nothing is linked', async () => {
    const { get } = mockApi({ status: { ...status, linked: false, login: null } });
    mount();
    expect(await screen.findByText(/Link your GitHub account on/)).toBeTruthy();
    expect(screen.getByRole('link', { name: 'Account' }).getAttribute('href')).toBe('/account');
    expect(get.mock.calls.some((c) => c[0] === '/api/v1/github/repos')).toBe(false);
  });

  it('shows the tabs with their counts and a row per repository with its last push', async () => {
    mockApi();
    mount();
    expect(await screen.findByRole('button', { name: 'moveeeax/life-os-cpp' })).toBeTruthy();
    const tabs = screen.getByRole('navigation', { name: 'GitHub pages' });
    expect(within(tabs).getByText('Needs attention').textContent).toBe('Needs attention12');
    expect(within(tabs).getByText('All').textContent).toBe('All378');
    expect(within(tabs).getByText('Archived').textContent).toBe('Archived4');
    expect(screen.getByText('Oct 30')).toBeTruthy();
    expect(screen.getByText('Feb 10, 2024')).toBeTruthy();
    expect(screen.getByText(/^synced .* · as moveeeax$/)).toBeTruthy();
    // The dot marks the one that needs attention.
    expect(document.querySelectorAll('.k-dot').length).toBe(1);
  });

  it('filters by owner from the chips and by the typed text, debounced', async () => {
    const { get } = mockApi();
    mount();
    await screen.findByRole('button', { name: 'moveeeax/life-os-cpp' });
    expect(lastListQuery(get)).toEqual({ view: 'attention', limit: 50, offset: 0 });
    fireEvent.click(screen.getByRole('button', { name: 'cybercapybara' }));
    await waitFor(() => expect(lastListQuery(get).owner).toBe('cybercapybara'));
    fireEvent.change(screen.getByRole('searchbox', { name: 'Search repositories' }), {
      target: { value: 'terraform' },
    });
    await waitFor(() => expect(lastListQuery(get).q).toBe('terraform'));
    expect(lastListQuery(get).owner).toBe('cybercapybara');
  });

  it('says when nothing needs attention', async () => {
    mockApi({ rows: [] });
    mount();
    expect(await screen.findByText('Nothing needs attention.')).toBeTruthy();
  });

  it('shows more with the next offset', async () => {
    const { get } = mockApi({ total: 120 });
    mount();
    fireEvent.click(await screen.findByRole('button', { name: 'Show more' }));
    await waitFor(() => expect(lastListQuery(get).offset).toBe(2));
  });
});

describe('GitHub sheet', () => {
  async function openSheet() {
    fireEvent.click(await screen.findByRole('button', { name: 'moveeeax/life-os-cpp' }));
    return await screen.findByRole('dialog', { name: 'moveeeax/life-os-cpp' });
  }

  it('shows the repository and its attention list', async () => {
    mockApi();
    mount();
    const dlg = await openSheet();
    expect(within(dlg).getByText('Life OS, the site: 3 services')).toBeTruthy();
    expect(within(dlg).getByText('public')).toBeTruthy();
    expect(within(dlg).getByText('MIT')).toBeTruthy();
    expect(within(dlg).getByText('42')).toBeTruthy();
    expect(within(dlg).getByText('checks fail')).toBeTruthy();
    expect(within(dlg).getByRole('list', { name: 'Topics' }).textContent).toBe('cppk8s');
    expect(within(dlg).getByText('Failing checks')).toBeTruthy();
    expect(within(dlg).getByText('3 open issues')).toBeTruthy();
    const gh = within(dlg).getByRole('link', { name: /Open on GitHub/ });
    expect(gh.getAttribute('href')).toBe('https://github.com/moveeeax/life-os-cpp');
    expect(gh.getAttribute('rel')).toBe('noopener noreferrer');
    expect(gh.getAttribute('target')).toBe('_blank');
  });

  it('makes a task from an item and opens the one that exists', async () => {
    const { post } = mockApi();
    mount();
    const dlg = await openSheet();
    // The checks rule has a task already: the button opens it.
    const buttons = within(dlg).getAllByRole('button', { name: /task/i });
    expect(buttons.map((b) => b.textContent)).toEqual(['Open task', 'Make a task']);
    fireEvent.click(buttons[1]);
    await waitFor(() =>
      expect(post).toHaveBeenCalledWith('/api/v1/github/repos/moveeeax/life-os-cpp/task', {
        body: { rule: 'issues' },
      }),
    );
    expect(await screen.findByText(/Task made/)).toBeTruthy();
    fireEvent.click(buttons[0]);
    expect(await screen.findByTestId('later')).toBeTruthy();
  });

  it('opens the task the server already had when created is false', async () => {
    const { post } = mockApi();
    post.mockResolvedValue({ data: { task_id: 't-old', created: false } });
    mount();
    const dlg = await openSheet();
    fireEvent.click(within(dlg).getByRole('button', { name: 'Make a task' }));
    expect(await screen.findByTestId('later')).toBeTruthy();
  });

  it('silences a rule and lets it speak again', async () => {
    const { put, del } = mockApi();
    mount();
    const dlg = await openSheet();
    fireEvent.click(within(dlg).getAllByRole('button', { name: 'Not about this' })[1]);
    await waitFor(() =>
      expect(put).toHaveBeenCalledWith('/api/v1/github/repos/moveeeax/life-os-cpp/mutes/issues'),
    );
    expect(await within(dlg).findByText(/Silenced:/)).toBeTruthy();
    fireEvent.click(within(dlg).getAllByRole('button', { name: 'Undo' })[0]);
    await waitFor(() =>
      expect(del).toHaveBeenCalledWith('/api/v1/github/repos/moveeeax/life-os-cpp/mutes/prs'),
    );
  });
});

describe('GitHub: Hide numbers covers every number', () => {
  it('on the list and in the sheet', async () => {
    mockApi();
    mount();
    fireEvent.click(await screen.findByRole('button', { name: 'moveeeax/life-os-cpp' }));
    await screen.findByRole('dialog');
    expect(document.body.textContent).toMatch(/\d/);
    expect(unmarked(document.body)).toEqual([]);
  });
});
