import { useState } from 'react';

import { useAddToHabit, useTickHabit } from '@/hooks/useHabits';
import { amountProblem, longDay, progressLine, quickAmounts, type HabitDays } from '@/lib/habits';
import { Num, Sheet } from '@/shell/kit';

/**
 * Add an amount to the day of a habit with a daily target (a glass of water),
 * or clear the day. The day counts once the sum reaches the target.
 */
export function AddSheet({
  habit,
  date,
  value,
  onClose,
}: {
  habit: HabitDays;
  date: string;
  value: number | null | undefined;
  onClose: () => void;
}) {
  const [amount, setAmount] = useState('');
  const [problem, setProblem] = useState<string | null>(null);
  const add = useAddToHabit(onClose);
  const clear = useTickHabit(onClose);
  const quick = quickAmounts(habit);
  const send = (v: number) => add.mutate({ id: habit.id, date, value: v });
  const busy = add.isPending || clear.isPending;
  return (
    <Sheet title={habit.name} onClose={onClose} width={400}>
      <p className="k-sub">
        <span data-num-ok>{longDay(date)}</span> · <Num>{progressLine(habit, value)}</Num>
      </p>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          const wrong = amountProblem(amount);
          setProblem(wrong);
          if (!wrong) send(Number(amount.replace(',', '.')));
        }}
      >
        {quick.length > 0 && (
          <div className="h-quick" role="group" aria-label="Add at once">
            {quick.map((q) => (
              <button
                key={q}
                type="button"
                className="k-btn"
                disabled={busy}
                onClick={() => send(q)}
              >
                + <Num>{q}</Num> {habit.unit}
              </button>
            ))}
          </div>
        )}
        <label className="k-field">
          <span>Amount{habit.unit ? `, ${habit.unit}` : ''}</span>
          <input
            inputMode="decimal"
            autoFocus
            placeholder={quick.length > 0 ? String(quick[0]) : ''}
            value={amount}
            onChange={(e) => setAmount(e.target.value)}
          />
        </label>
        {(problem || add.error || clear.error) && (
          <p role="alert" className="k-error">
            {problem ?? add.error ?? clear.error}
          </p>
        )}
        <div className="k-dlg-actions">
          {value != null && (
            <button
              type="button"
              className="k-link k-error-text"
              disabled={busy}
              onClick={() => clear.mutate({ id: habit.id, date, on: false })}
            >
              Clear the day
            </button>
          )}
          <span className="k-grow" />
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={busy}>
            {add.isPending ? 'Adding…' : 'Add'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}
