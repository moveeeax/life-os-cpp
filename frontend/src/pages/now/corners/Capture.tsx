import { useQueryClient } from '@tanstack/react-query';
import { useState } from 'react';
import { Link } from 'react-router';

import { useCreateEntries, useParseJob } from '@/hooks/useFood';
import { useAcceptParse, useMoneyParse } from '@/hooks/useMoney';
import { qk } from '@/lib/api/queryKeys';
import {
  draftFromJob as foodDraft,
  draftProblem as foodProblem,
  linesToEntries,
  mealByHour,
} from '@/lib/food';
import {
  draftFromJob as moneyDraft,
  draftProblem as moneyProblem,
  linesToAccept,
  parseErrorText,
} from '@/lib/ledger';

type Kind = 'meal' | 'spend';

/**
 * One line of text into the Food or Money parse, its lines here and one Add.
 * A meal goes into the day; a spend lands in the Money inbox. A spend the
 * parse could not finish (no account, no category) is finished on Money.
 */
export function Capture({ date, hour }: { date: string; hour: number }) {
  const [kind, setKind] = useState<Kind>('meal');
  const [text, setText] = useState('');
  const food = useParseJob();
  const money = useMoneyParse();
  const queryClient = useQueryClient();
  const refresh = () => {
    setText('');
    food.cancel();
    money.cancel();
    void queryClient.invalidateQueries({ queryKey: qk.now.days() });
  };
  const addMeal = useCreateEntries(refresh);
  const addSpend = useAcceptParse(money.job?.id, refresh);
  const meal = mealByHour(hour);

  const parse = kind === 'meal' ? food : money;
  const busy = parse.state === 'starting' || parse.state === 'running';

  const submit = () => {
    const t = text.trim();
    if (!t || busy) return;
    if (kind === 'meal') void food.start({ text: t, meal, date });
    else void money.start({ kind: 'text', text: t, hint_date: date });
  };
  const switchTo = (k: Kind) => {
    food.cancel();
    money.cancel();
    setKind(k);
  };

  const mealLines = kind === 'meal' && food.state === 'done' && food.job ? foodDraft(food.job) : [];
  const spendLines =
    kind === 'spend' && money.state === 'done' && money.job ? moneyDraft(money.job) : [];
  const mealBlocked = mealLines.length ? foodProblem(mealLines) : null;
  const spendBlocked = spendLines.length ? moneyProblem(spendLines) : null;
  const failure =
    parse.state === 'failed'
      ? parse.error === 'parse_timeout'
        ? 'The parse took too long. Try again or use the full form.'
        : kind === 'spend'
          ? parseErrorText(parse.error)
          : (parse.error ?? 'The parse failed.')
      : null;

  return (
    <div className="c-capture">
      <div className="c-capture-kind" role="radiogroup" aria-label="What to add">
        {(['meal', 'spend'] as const).map((k) => (
          <button
            key={k}
            type="button"
            role="radio"
            aria-checked={kind === k}
            className={kind === k ? 'c-on' : undefined}
            onClick={() => switchTo(k)}
          >
            {k === 'meal' ? 'Meal' : 'Spend'}
          </button>
        ))}
      </div>
      <input
        className="c-capture-input"
        aria-label={kind === 'meal' ? 'Describe a meal' : 'Describe a spend'}
        placeholder={kind === 'meal' ? 'Omelette and a toast' : 'Coffee 2500'}
        maxLength={2000}
        value={text}
        disabled={busy}
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            submit();
          }
        }}
      />
      {busy && <p className="c-empty">Reading…</p>}
      {failure && (
        <p role="alert" className="c-warn-text">
          {failure}
        </p>
      )}
      {mealLines.map((l) => (
        <div key={l.key} className="c-row c-plain">
          <span className="c-t">{l.name}</span>
          <span className="c-r">
            <span className="num">{l.kcal}</span> kcal
          </span>
        </div>
      ))}
      {spendLines.map((l) => (
        <div key={l.key} className="c-row c-plain">
          <span className="c-t">{l.name || l.merchant}</span>
          <span className="c-r num">{l.amount}</span>
        </div>
      ))}
      {mealLines.length > 0 &&
        (mealBlocked ? (
          <p className="c-warn-text">{mealBlocked}</p>
        ) : (
          <button
            type="button"
            className="c-add"
            disabled={addMeal.isPending}
            onClick={() => addMeal.mutate(linesToEntries(mealLines, date, meal))}
          >
            Add to {meal}
          </button>
        ))}
      {spendLines.length > 0 &&
        (spendBlocked ? (
          <p className="c-warn-text">
            {spendBlocked}{' '}
            <Link className="c-link" to="/money">
              Finish on Money →
            </Link>
          </p>
        ) : (
          <button
            type="button"
            className="c-add"
            disabled={addSpend.isPending}
            onClick={() => addSpend.mutate(linesToAccept(spendLines))}
          >
            Add to the inbox
          </button>
        ))}
      {(addMeal.error || addSpend.error) && (
        <p role="alert" className="c-warn-text">
          {addMeal.error ?? addSpend.error}
        </p>
      )}
    </div>
  );
}
