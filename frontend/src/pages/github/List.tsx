import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router';

import { useGithubRepos, useGithubStatus } from '@/hooks/useGithub';
import { todayLocal } from '@/lib/dates';
import { pushedLabel, syncedAgo, type GithubRepo, type GithubView } from '@/lib/github';
import { Num, Row } from '@/shell/kit';
import { secondaryButton } from '@/shell/kit/styles';

import { GithubFrame, LoadError, Placeholder } from './frame';
import { RepoSheet } from './Sheets';
import { githubTabs } from './tabs';

/**
 * The search field: it keeps the typed text and its 300 ms debounce to itself
 * and lifts only the settled query, so a keystroke renders this field, not the list.
 */
function SearchBox({ onSearch }: { onSearch: (q: string) => void }) {
  const [text, setText] = useState('');
  useEffect(() => {
    const t = setTimeout(() => onSearch(text.trim()), 300);
    return () => clearTimeout(t);
  }, [text, onSearch]);
  return (
    <input
      className="k-input k-search"
      type="search"
      aria-label="Search repositories"
      placeholder="Name or topic"
      value={text}
      onChange={(e) => setText(e.target.value)}
    />
  );
}

function RepoRow({ r, today, onOpen }: { r: GithubRepo; today: string; onOpen: () => void }) {
  return (
    <Row
      dot={r.attention.length > 0 ? 'var(--g-warn)' : undefined}
      title={
        <button type="button" className="k-row-link gh-name" onClick={onOpen} data-num-ok>
          {r.full_name}
        </button>
      }
      sub={<span data-num-ok>{pushedLabel(r.pushed_at, today)}</span>}
    />
  );
}

const HEADLINE: Record<GithubView, (n: number) => string> = {
  attention: (n) => (n === 1 ? 'repository needs attention' : 'repositories need attention'),
  all: (n) => (n === 1 ? 'repository' : 'repositories'),
  archived: () => 'archived',
};

const EMPTY: Record<GithubView, string> = {
  attention: 'Nothing needs attention.',
  all: 'No repositories yet: the first sync is on its way.',
  archived: 'No archived repositories.',
};

/** One view of the repositories: calm rows, the details in a sheet on click. */
export function GithubPage({ view }: { view: GithubView }) {
  const today = todayLocal();
  const status = useGithubStatus();
  const linked = status.data?.linked === true;
  const [owner, setOwner] = useState('');
  const [q, setQ] = useState('');
  const onSearch = useCallback((next: string) => setQ(next), []);
  const list = useGithubRepos({ view, owner, q }, linked);
  const [open, setOpen] = useState<GithubRepo | null>(null);
  const first = list.data?.pages[0];
  const rows = list.data?.pages.flatMap((p) => p.data) ?? [];
  const owners = first?.owners ?? [];
  const count = first?.counts[view];
  const filtered = q !== '' || owner !== '';

  return (
    <GithubFrame
      headline={
        count === undefined ? (
          'GitHub'
        ) : (
          <>
            <Num>{count}</Num> {HEADLINE[view](count)}
          </>
        )
      }
      sub={
        status.data?.linked ? (
          <span data-num-ok>
            {syncedAgo(status.data.synced_at)}
            {status.data.login ? ` · as ${status.data.login}` : ''}
          </span>
        ) : undefined
      }
      tabs={linked ? githubTabs(first?.counts) : undefined}
      tools={
        linked ? (
          <>
            <SearchBox onSearch={onSearch} />
            {owners.length > 1 && (
              <div className="k-seg k-seg-scroll" role="group" aria-label="Owner">
                <button
                  type="button"
                  aria-pressed={owner === ''}
                  className={owner === '' ? 'k-seg-on' : undefined}
                  onClick={() => setOwner('')}
                >
                  All
                </button>
                {owners.map((o) => (
                  <button
                    key={o}
                    type="button"
                    aria-pressed={owner === o}
                    className={owner === o ? 'k-seg-on' : undefined}
                    onClick={() => setOwner(owner === o ? '' : o)}
                    data-num-ok
                  >
                    {o}
                  </button>
                ))}
              </div>
            )}
          </>
        ) : undefined
      }
    >
      {status.isPending ? (
        <Placeholder />
      ) : status.isError ? (
        <LoadError error={status.error} onRetry={() => void status.refetch()} />
      ) : !linked ? (
        <p className="k-empty">
          {status.data.configured
            ? 'Link your GitHub account on '
            : 'The server has no GitHub OAuth App yet; see '}
          <Link to="/account" className="k-link">
            Account
          </Link>
          .
        </p>
      ) : list.isPending ? (
        <Placeholder />
      ) : list.isError ? (
        <LoadError error={list.error} onRetry={() => void list.refetch()} />
      ) : rows.length === 0 ? (
        <p className="k-empty">{filtered ? 'No repository matches.' : EMPTY[view]}</p>
      ) : (
        <>
          <div className="k-group-rows">
            {rows.map((r) => (
              <RepoRow key={r.full_name} r={r} today={today} onOpen={() => setOpen(r)} />
            ))}
          </div>
          {list.hasNextPage && (
            <button
              type="button"
              onClick={() => void list.fetchNextPage()}
              disabled={list.isFetchingNextPage}
              className={`${secondaryButton} self-center`}
            >
              {list.isFetchingNextPage ? 'Loading…' : 'Show more'}
            </button>
          )}
        </>
      )}
      {open && <RepoSheet seed={open} onClose={() => setOpen(null)} />}
    </GithubFrame>
  );
}

export const GithubAttentionPage = () => <GithubPage view="attention" />;
export const GithubAllPage = () => <GithubPage view="all" />;
export const GithubArchivedPage = () => <GithubPage view="archived" />;
