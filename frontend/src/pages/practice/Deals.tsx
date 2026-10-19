import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useDealTasks, useDeals, useDeleteDeal, useMonth } from '@/hooks/usePractice';
import {
  amountsLine,
  dealDates,
  dealsByStage,
  money,
  monthOf,
  OPEN_STAGES,
  STAGE_GROUP,
  STAGE_LABEL,
  shortDay,
  todayLocal,
  type PracticeDeal,
} from '@/lib/practice';
import { Facts, Group, Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, PracticeFrame, Tools } from './frame';
import { DealSheet, EntrySheet, NextStepSheet } from './Sheets';

/** The open tasks of a deal, read when its row opens. */
function NextSteps({ deal, today }: { deal: PracticeDeal; today: string }) {
  const tasks = useDealTasks(deal.id, true);
  const open = (tasks.data ?? []).filter((t) => t.status !== 'done');
  if (tasks.isPending) return <span className="k-muted">Loading the next steps…</span>;
  if (open.length === 0) return <span className="k-muted">No next step yet.</span>;
  return (
    <ul className="k-plain-list">
      {open.map((t) => (
        <li key={t.id}>
          {t.title}
          {t.due && (
            <span className="k-muted" data-num-ok>
              {' '}
              · {shortDay(t.due, today)}
            </span>
          )}
        </li>
      ))}
    </ul>
  );
}

function DealRow({
  deal,
  today,
  onEdit,
  onDelete,
  onStep,
  onIncome,
}: {
  deal: PracticeDeal;
  today: string;
  onEdit: () => void;
  onDelete: () => void;
  onStep: () => void;
  onIncome: () => void;
}) {
  const [open, setOpen] = useState(false);
  const dates = dealDates(deal, today);
  return (
    <Row
      title={deal.title}
      sub={
        <>
          {deal.client_name}
          {dates && <span data-num-ok> · {dates}</span>}
        </>
      }
      right={
        deal.value != null && deal.currency ? (
          <Num>{money(deal.value, deal.currency)}</Num>
        ) : undefined
      }
      open={open}
      onToggle={setOpen}
    >
      <Facts
        items={[
          { label: 'Stage', value: STAGE_LABEL[deal.stage] },
          {
            label: 'Received',
            value: deal.received.length ? <Num>{amountsLine(deal.received)}</Num> : 'nothing yet',
          },
          { label: 'Open tasks', value: <Num>{deal.open_tasks}</Num> },
        ]}
      />
      {open && <NextSteps deal={deal} today={today} />}
      {deal.note && <span>{deal.note}</span>}
      <span className="k-actions">
        <button type="button" className="k-link" onClick={onStep}>
          Add a next step
        </button>
        <button type="button" className="k-link" onClick={onIncome}>
          Add an income
        </button>
        <button type="button" className="k-link" onClick={onEdit}>
          Edit
        </button>
        <button type="button" className="k-link k-error-text" onClick={onDelete}>
          Delete
        </button>
      </span>
    </Row>
  );
}

/** The deals by stage, open ones first; this month's income in the line under the headline. */
export function PracticeDealsPage() {
  const today = todayLocal();
  const deals = useDeals();
  const month = useMonth(monthOf(today));
  const [editing, setEditing] = useState<PracticeDeal | 'new' | null>(null);
  const [step, setStep] = useState<PracticeDeal | null>(null);
  const [income, setIncome] = useState<PracticeDeal | null>(null);
  const [deleting, setDeleting] = useState<PracticeDeal | null>(null);
  const remove = useDeleteDeal(() => setDeleting(null));
  useErrorToast(remove.error);
  const list = deals.data ?? [];
  const openCount = list.filter((d) => OPEN_STAGES.includes(d.stage)).length;
  const earned = (month.data?.totals ?? [])
    .filter((t) => t.income > 0)
    .map((t) => ({ currency: t.currency, amount: t.income }));
  return (
    <PracticeFrame
      headline={
        openCount === 0 ? (
          'Practice'
        ) : (
          <>
            <Num>{openCount}</Num> {openCount === 1 ? 'open deal' : 'open deals'}
          </>
        )
      }
      sub={
        earned.length > 0 ? (
          <>
            this month <Num>{amountsLine(earned)}</Num>
          </>
        ) : (
          'clients, deals and the money of the practice'
        )
      }
      actions={
        <Tools>
          <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
            New deal
          </button>
        </Tools>
      }
    >
      {deals.isPending ? (
        <Placeholder />
      ) : deals.isError ? (
        <LoadError error={deals.error} onRetry={() => void deals.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">
          No deals yet. A deal is a piece of work for a client, from a lead to done.
        </p>
      ) : (
        dealsByStage(list).map((g) => (
          <Group key={g.stage} label={STAGE_GROUP[g.stage]}>
            {g.deals.map((d) => (
              <DealRow
                key={d.id}
                deal={d}
                today={today}
                onEdit={() => setEditing(d)}
                onDelete={() => setDeleting(d)}
                onStep={() => setStep(d)}
                onIncome={() => setIncome(d)}
              />
            ))}
          </Group>
        ))
      )}
      {editing && (
        <DealSheet
          deal={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
      {step && <NextStepSheet deal={step} onClose={() => setStep(null)} />}
      {income && (
        <EntrySheet
          initial={{
            kind: 'income',
            deal_id: income.id,
            currency: income.currency ?? 'USD',
          }}
          onClose={() => setIncome(null)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.title}?`}
          description="Its incomes, expenses and tasks stay, without the deal. This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
    </PracticeFrame>
  );
}
