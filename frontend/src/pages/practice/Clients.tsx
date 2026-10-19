import { useState } from 'react';
import { Link } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useErrorToast } from '@/hooks/useErrorToast';
import { useClients, useDeleteClient } from '@/hooks/usePractice';
import { amountsLine, type PracticeClient } from '@/lib/practice';
import { Facts, Group, Num, Row } from '@/shell/kit';

import { LoadError, Placeholder, PracticeFrame, Tools } from './frame';
import { ClientSheet, DealSheet } from './Sheets';

/** The clients of the practice by name; their income per currency on click. */
export function PracticeClientsPage() {
  const clients = useClients();
  const [editing, setEditing] = useState<PracticeClient | 'new' | null>(null);
  const [dealFor, setDealFor] = useState<PracticeClient | null>(null);
  const [deleting, setDeleting] = useState<PracticeClient | null>(null);
  const remove = useDeleteClient(() => setDeleting(null));
  useErrorToast(remove.error);
  const list = clients.data ?? [];
  return (
    <PracticeFrame
      headline="Clients"
      sub={
        list.length > 0 ? (
          <>
            <Num>{list.length}</Num> {list.length === 1 ? 'client' : 'clients'}
          </>
        ) : undefined
      }
      actions={
        <Tools>
          <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
            New client
          </button>
        </Tools>
      }
    >
      {clients.isPending ? (
        <Placeholder />
      ) : clients.isError ? (
        <LoadError error={clients.error} onRetry={() => void clients.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">No clients yet. A client holds its deals and its payments.</p>
      ) : (
        <Group label="By name">
          {list.map((c) => (
            <Row
              key={c.id}
              title={c.name}
              sub={c.contact || undefined}
              right={
                c.open_deals > 0 ? (
                  <span className="k-muted">
                    <Num>{c.open_deals}</Num> open
                  </span>
                ) : undefined
              }
            >
              <Facts
                items={[
                  {
                    label: 'Paid so far',
                    value: c.income.length ? <Num>{amountsLine(c.income)}</Num> : 'nothing yet',
                  },
                ]}
              />
              {c.note && <span>{c.note}</span>}
              <span className="k-actions">
                <button type="button" className="k-link" onClick={() => setDealFor(c)}>
                  New deal
                </button>
                <Link to={`/practice/money?client=${c.id}`} className="k-link">
                  Payments
                </Link>
                <button type="button" className="k-link" onClick={() => setEditing(c)}>
                  Edit
                </button>
                <button
                  type="button"
                  className="k-link k-error-text"
                  onClick={() => setDeleting(c)}
                >
                  Delete
                </button>
              </span>
            </Row>
          ))}
        </Group>
      )}
      {editing && (
        <ClientSheet
          client={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
      {dealFor && <DealSheet clientId={dealFor.id} onClose={() => setDealFor(null)} />}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.name}?`}
          description="A client with deals stays until its deals are gone; its payments stay, without the client."
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
