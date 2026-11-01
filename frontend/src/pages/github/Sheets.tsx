import { useNavigate } from 'react-router';

import { useToast } from '@/components/ui/toaster';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useGithubMute, useGithubRepo, useGithubTask } from '@/hooks/useGithub';
import { todayLocal } from '@/lib/dates';
import {
  CHECKS_LABEL,
  RULE_LABEL,
  kindLine,
  pushedLabel,
  repoUrl,
  taskLink,
  type GithubAttentionItem,
  type GithubRepo,
  type GithubRule,
} from '@/lib/github';
import { Facts, Num, Sheet } from '@/shell/kit';

/** One reason of the attention list with its two actions. */
function AttentionItem({
  repo,
  item,
  mute,
  task,
}: {
  repo: GithubRepo;
  item: GithubAttentionItem;
  mute: ReturnType<typeof useGithubMute>;
  task: ReturnType<typeof useGithubTask>;
}) {
  const navigate = useNavigate();
  const toast = useToast();
  const existing = repo.tasks[item.rule];
  const busy = task.isPending && task.variables === item.rule;
  const makeOrOpen = () => {
    if (existing) {
      navigate(taskLink(existing));
      return;
    }
    task.mutate(item.rule, {
      onSuccess: (d) => {
        if (d.created)
          toast.success(`Task made: ${repo.name}, ${RULE_LABEL[item.rule].toLowerCase()}`);
        else navigate(taskLink(d.task_id));
      },
    });
  };
  return (
    <li className="gh-item">
      <span className="gh-item-head">
        <span>{RULE_LABEL[item.rule]}</span>
        <Num className="k-muted">{item.detail}</Num>
      </span>
      <span className="k-actions">
        <button type="button" className="k-link" disabled={busy} onClick={makeOrOpen}>
          {busy ? 'Making…' : existing ? 'Open task' : 'Make a task'}
        </button>
        <button
          type="button"
          className="k-link"
          disabled={mute.isPending}
          onClick={() => mute.mutate({ rule: item.rule, muted: true })}
        >
          Not about this
        </button>
      </span>
    </li>
  );
}

/**
 * One repository: what it is, what needs attention and the two actions per
 * item; a silenced rule can speak again from the line under the list.
 */
export function RepoSheet({ seed, onClose }: { seed: GithubRepo; onClose: () => void }) {
  const today = todayLocal();
  const repo = useGithubRepo(seed.full_name, seed).data ?? seed;
  const mute = useGithubMute(repo.full_name);
  const task = useGithubTask(repo.full_name);
  useErrorToast(mute.error);
  useErrorToast(task.error);
  const facts = [
    { label: 'Kind', value: <span data-num-ok>{kindLine(repo)}</span> },
    { label: 'License', value: <span data-num-ok>{repo.license || 'none'}</span> },
    { label: 'Stars', value: <Num>{repo.stars}</Num> },
    { label: 'Checks', value: <span data-num-ok>{CHECKS_LABEL[repo.checks]}</span> },
    { label: 'Last push', value: <span data-num-ok>{pushedLabel(repo.pushed_at, today)}</span> },
  ];
  return (
    <Sheet title={repo.full_name} onClose={onClose} width={520}>
      <div className="k-form">
        {repo.description ? (
          <p className="gh-description" data-num-ok>
            {repo.description}
          </p>
        ) : (
          <p className="k-muted">No description.</p>
        )}
        <Facts items={facts} />
        {repo.topics.length > 0 && (
          <ul className="gh-topics" aria-label="Topics" data-num-ok>
            {repo.topics.map((t) => (
              <li key={t}>{t}</li>
            ))}
          </ul>
        )}
        <section>
          <h3 className="k-group-label">
            <span>Needs attention</span>
          </h3>
          {repo.attention.length === 0 ? (
            <p className="k-muted">Nothing. {repo.archived ? 'Archived.' : 'All quiet here.'}</p>
          ) : (
            <ul className="k-plain-list">
              {repo.attention.map((item) => (
                <AttentionItem key={item.rule} repo={repo} item={item} mute={mute} task={task} />
              ))}
            </ul>
          )}
          {repo.muted.length > 0 && (
            <p className="k-muted">
              Silenced:{' '}
              {repo.muted.map((rule: GithubRule, i) => (
                <span key={rule}>
                  {i > 0 && ', '}
                  {RULE_LABEL[rule].toLowerCase()}{' '}
                  <button
                    type="button"
                    className="k-link"
                    disabled={mute.isPending}
                    onClick={() => mute.mutate({ rule, muted: false })}
                  >
                    Undo
                  </button>
                </span>
              ))}
            </p>
          )}
        </section>
        <div className="k-dlg-actions">
          <a
            className="k-btn"
            href={repoUrl(repo.full_name)}
            target="_blank"
            rel="noopener noreferrer"
          >
            Open on GitHub ↗
          </a>
          <button type="button" className="k-btn k-solid" onClick={onClose}>
            Close
          </button>
        </div>
      </div>
    </Sheet>
  );
}
