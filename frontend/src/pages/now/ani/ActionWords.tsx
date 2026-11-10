import { formatDay } from '@/lib/dates';

import { titleAndDay, type Action } from './chat';

/**
 * What a proposal changes, in words: a meal or expenses as their title with
 * the day under it, any other kind as its one line (@p words, the summary
 * unless given).
 */
export function ActionWords({
  action,
  words,
  className,
}: {
  action: Pick<Action, 'kind' | 'summary'>;
  words?: string;
  className?: string;
}) {
  const split = titleAndDay(action);
  if (!split) {
    return (
      <span className={className ? `${className} num` : 'num'}>{words ?? action.summary}</span>
    );
  }
  return (
    <span className={className ? `${className} ani-act-two` : 'ani-act-two'}>
      <span className="num">{split.title}</span>
      <time className="ani-act-day" dateTime={split.day} data-num-ok>
        {formatDay(split.day)}
      </time>
    </span>
  );
}
