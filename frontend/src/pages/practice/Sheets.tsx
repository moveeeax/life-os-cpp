import { useState } from 'react';

import {
  useAddNextStep,
  useClients,
  useDeals,
  useSaveClient,
  useSaveDeal,
  useSaveEntry,
} from '@/hooks/usePractice';
import {
  dealBody,
  entryBody,
  OPEN_STAGES,
  STAGE_LABEL,
  STAGES,
  todayLocal,
  type DealForm,
  type EntryForm,
  type PracticeClient,
  type PracticeDeal,
  type PracticeEntry,
} from '@/lib/practice';
import { Sheet } from '@/shell/kit';

function Actions({
  busy,
  label,
  onCancel,
}: {
  busy: boolean;
  label: string;
  onCancel: () => void;
}) {
  return (
    <div className="k-dlg-actions">
      <button type="button" className="k-btn" onClick={onCancel}>
        Cancel
      </button>
      <button type="submit" className="k-btn k-solid" disabled={busy}>
        {busy ? 'Saving…' : label}
      </button>
    </div>
  );
}

function ErrorLine({ error }: { error: string | null }) {
  return error ? (
    <p role="alert" className="k-error">
      {error}
    </p>
  ) : null;
}

/** Add or edit a client. */
export function ClientSheet({
  client,
  onClose,
}: {
  client?: PracticeClient;
  onClose: (saved?: PracticeClient) => void;
}) {
  const [f, setF] = useState({
    name: client?.name ?? '',
    contact: client?.contact ?? '',
    note: client?.note ?? '',
  });
  const save = useSaveClient(client?.id ?? null, (c) => onClose(c));
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  return (
    <Sheet title={client ? 'Edit the client' : 'New client'} onClose={() => onClose()} width={440}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          save.mutate({ name: f.name.trim(), contact: f.contact.trim(), note: f.note.trim() });
        }}
      >
        <label className="k-field">
          <span>Name</span>
          <input
            required
            maxLength={120}
            placeholder="Acme"
            value={f.name}
            onChange={set('name')}
          />
        </label>
        <label className="k-field">
          <span>Contact</span>
          <input
            maxLength={300}
            placeholder="An email, a phone or a link"
            value={f.contact}
            onChange={set('contact')}
          />
        </label>
        <label className="k-field">
          <span>Note</span>
          <textarea rows={3} maxLength={2000} value={f.note} onChange={set('note')} />
        </label>
        <ErrorLine error={save.error} />
        <Actions busy={save.isPending} label={client ? 'Save' : 'Add'} onCancel={() => onClose()} />
      </form>
    </Sheet>
  );
}

/** Add or edit a deal; a deal needs a client first. */
export function DealSheet({
  deal,
  clientId,
  onClose,
}: {
  deal?: PracticeDeal;
  clientId?: string;
  onClose: () => void;
}) {
  const clients = useClients().data ?? [];
  const [f, setF] = useState<DealForm>({
    client_id: deal?.client_id ?? clientId ?? '',
    title: deal?.title ?? '',
    stage: deal?.stage ?? 'lead',
    value: deal?.value != null ? String(deal.value) : '',
    currency: deal?.currency ?? 'USD',
    start_date: deal?.start_date ?? '',
    end_date: deal?.end_date ?? '',
    note: deal?.note ?? '',
  });
  const [problem, setProblem] = useState<string | null>(null);
  const save = useSaveDeal(deal?.id ?? null, onClose);
  const set = (k: keyof DealForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  const client = f.client_id || clients[0]?.id || '';
  return (
    <Sheet title={deal ? 'Edit the deal' : 'New deal'} onClose={onClose} width={480}>
      {clients.length === 0 ? (
        <div className="k-form">
          <p className="k-empty">A deal is for a client: add the client first, on Clients.</p>
          <div className="k-dlg-actions">
            <button type="button" className="k-btn" onClick={onClose}>
              Close
            </button>
          </div>
        </div>
      ) : (
        <form
          className="k-form"
          onSubmit={(e) => {
            e.preventDefault();
            const out = dealBody({ ...f, client_id: client });
            if ('error' in out) {
              setProblem(out.error);
              return;
            }
            setProblem(null);
            save.mutate(out.body);
          }}
        >
          <label className="k-field">
            <span>Title</span>
            <input
              required
              maxLength={200}
              placeholder="Landing page"
              value={f.title}
              onChange={set('title')}
            />
          </label>
          <div className="k-fields">
            <label className="k-field">
              <span>Client</span>
              <select value={client} onChange={set('client_id')}>
                {clients.map((c) => (
                  <option key={c.id} value={c.id}>
                    {c.name}
                  </option>
                ))}
              </select>
            </label>
            <label className="k-field">
              <span>Stage</span>
              <select value={f.stage} onChange={set('stage')}>
                {STAGES.map((s) => (
                  <option key={s} value={s}>
                    {STAGE_LABEL[s]}
                  </option>
                ))}
              </select>
            </label>
          </div>
          <div className="k-fields">
            <label className="k-field">
              <span>Value</span>
              <input
                inputMode="decimal"
                placeholder="1500"
                className="num"
                value={f.value}
                onChange={set('value')}
              />
            </label>
            <label className="k-field">
              <span>Currency</span>
              <input
                maxLength={3}
                placeholder="USD"
                value={f.currency}
                onChange={(e) => setF({ ...f, currency: e.target.value.toUpperCase() })}
              />
            </label>
          </div>
          <div className="k-fields">
            <label className="k-field">
              <span>Starts</span>
              <input type="date" value={f.start_date} onChange={set('start_date')} />
            </label>
            <label className="k-field">
              <span>Ends</span>
              <input type="date" value={f.end_date} onChange={set('end_date')} />
            </label>
          </div>
          <label className="k-field">
            <span>Note</span>
            <textarea rows={3} maxLength={2000} value={f.note} onChange={set('note')} />
          </label>
          <ErrorLine error={problem ?? save.error} />
          <Actions busy={save.isPending} label={deal ? 'Save' : 'Add'} onCancel={onClose} />
        </form>
      )}
    </Sheet>
  );
}

/** Add or edit an income or an expense; a deal brings its client. */
export function EntrySheet({
  entry,
  initial,
  onClose,
}: {
  entry?: PracticeEntry;
  initial?: Partial<EntryForm>;
  onClose: () => void;
}) {
  const clients = useClients().data ?? [];
  const deals = useDeals().data ?? [];
  const [f, setF] = useState<EntryForm>({
    kind: entry?.kind ?? initial?.kind ?? 'income',
    date: entry?.date ?? initial?.date ?? todayLocal(),
    amount: entry ? String(entry.amount) : (initial?.amount ?? ''),
    currency: entry?.currency ?? initial?.currency ?? 'USD',
    client_id: entry?.client_id ?? initial?.client_id ?? '',
    deal_id: entry?.deal_id ?? initial?.deal_id ?? '',
    note: entry?.note ?? initial?.note ?? '',
  });
  const [problem, setProblem] = useState<string | null>(null);
  const save = useSaveEntry(entry?.id ?? null, onClose);
  const set = (k: keyof EntryForm) => (e: { target: { value: string } }) =>
    setF({ ...f, [k]: e.target.value });
  // Open deals first; a closed deal stays pickable for a late payment.
  const pickable = [...deals].sort(
    (a, b) => Number(!OPEN_STAGES.includes(a.stage)) - Number(!OPEN_STAGES.includes(b.stage)),
  );
  const title = entry
    ? f.kind === 'income'
      ? 'Edit the income'
      : 'Edit the expense'
    : f.kind === 'income'
      ? 'New income'
      : 'New expense';
  return (
    <Sheet title={title} onClose={onClose} width={480}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          const out = entryBody(f);
          if ('error' in out) {
            setProblem(out.error);
            return;
          }
          setProblem(null);
          save.mutate(out.body);
        }}
      >
        <div className="k-fields">
          <label className="k-field">
            <span>Kind</span>
            <select value={f.kind} onChange={set('kind')}>
              <option value="income">Income</option>
              <option value="expense">Expense</option>
            </select>
          </label>
          <label className="k-field">
            <span>Day</span>
            <input type="date" required value={f.date} onChange={set('date')} />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Amount</span>
            <input
              required
              inputMode="decimal"
              placeholder="1500"
              className="num"
              value={f.amount}
              onChange={set('amount')}
            />
          </label>
          <label className="k-field">
            <span>Currency</span>
            <input
              required
              maxLength={3}
              placeholder="USD"
              value={f.currency}
              onChange={(e) => setF({ ...f, currency: e.target.value.toUpperCase() })}
            />
          </label>
        </div>
        <div className="k-fields">
          <label className="k-field">
            <span>Deal</span>
            <select value={f.deal_id} onChange={set('deal_id')}>
              <option value="">No deal</option>
              {pickable.map((d) => (
                <option key={d.id} value={d.id}>
                  {d.title} · {d.client_name}
                </option>
              ))}
            </select>
          </label>
          <label className="k-field">
            <span>Client</span>
            <select
              value={f.deal_id ? '' : f.client_id}
              disabled={!!f.deal_id}
              onChange={set('client_id')}
            >
              <option value="">{f.deal_id ? "The deal's client" : 'No client'}</option>
              {clients.map((c) => (
                <option key={c.id} value={c.id}>
                  {c.name}
                </option>
              ))}
            </select>
          </label>
        </div>
        <label className="k-field">
          <span>Note</span>
          <input
            maxLength={500}
            placeholder="Invoice 12, hosting"
            value={f.note}
            onChange={set('note')}
          />
        </label>
        <ErrorLine error={problem ?? save.error} />
        <Actions busy={save.isPending} label={entry ? 'Save' : 'Add'} onCancel={onClose} />
      </form>
    </Sheet>
  );
}

/** A next step of a deal: a task with the deal's id, in the Projects area. */
export function NextStepSheet({ deal, onClose }: { deal: PracticeDeal; onClose: () => void }) {
  const [title, setTitle] = useState('');
  const [due, setDue] = useState('');
  const add = useAddNextStep(onClose);
  return (
    <Sheet title={`Next step for ${deal.title}`} onClose={onClose} width={440}>
      <form
        className="k-form"
        onSubmit={(e) => {
          e.preventDefault();
          add.mutate({ title: title.trim(), area: 'projects', due: due || null, deal_id: deal.id });
        }}
      >
        <label className="k-field">
          <span>Task</span>
          <input
            required
            maxLength={200}
            placeholder="Send the estimate"
            value={title}
            onChange={(e) => setTitle(e.target.value)}
          />
        </label>
        <label className="k-field">
          <span>Due</span>
          <input type="date" value={due} onChange={(e) => setDue(e.target.value)} />
        </label>
        <ErrorLine error={add.error} />
        <Actions busy={add.isPending} label="Add" onCancel={onClose} />
      </form>
    </Sheet>
  );
}
