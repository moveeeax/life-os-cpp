import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteDocument, useDocuments } from '@/hooks/useTravel';
import { useErrorToast } from '@/hooks/useErrorToast';
import {
  DOCUMENT_KIND,
  DOCUMENT_SOON_DAYS,
  daysBetween,
  shortDay,
  todayLocal,
  type TravelDocument,
} from '@/lib/travel';
import { Group, Row } from '@/shell/kit';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { DocumentSheet } from './Sheets';

/** The family's passports, visas and insurance, the soonest to expire first. */
export function TravelDocumentsPage() {
  const today = todayLocal();
  const docs = useDocuments();
  const [deleting, setDeleting] = useState<TravelDocument | null>(null);
  const remove = useDeleteDocument(() => setDeleting(null));
  useErrorToast(remove.error);
  const [editing, setEditing] = useState<TravelDocument | 'new' | null>(null);
  const list = docs.data ?? [];
  const soon = list.filter((d) => daysBetween(today, d.valid_until) <= DOCUMENT_SOON_DAYS);
  return (
    <TravelFrame
      title="Documents"
      headline={
        soon.length === 0
          ? 'Documents'
          : soon.length === 1
            ? `${soon[0].holder}'s ${DOCUMENT_KIND[soon[0].kind].toLowerCase()} needs a look`
            : `${soon.length} documents need a look`
      }
      sub="checked against the dates of each trip"
      actions={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
            New document
          </button>
        </>
      }
    >
      {docs.isPending ? (
        <Placeholder />
      ) : docs.isError ? (
        <LoadError error={docs.error} onRetry={() => void docs.refetch()} />
      ) : list.length === 0 ? (
        <p className="k-empty">No documents yet: passports, visas, insurance.</p>
      ) : (
        <Group label="By expiry">
          {list.map((d) => {
            const left = daysBetween(today, d.valid_until);
            return (
              <Row
                key={d.id}
                dot={
                  left < 0
                    ? 'var(--g-late)'
                    : left <= DOCUMENT_SOON_DAYS
                      ? 'var(--g-warn)'
                      : 'var(--g-ink-faint)'
                }
                title={`${d.holder} · ${DOCUMENT_KIND[d.kind]}`}
                sub={
                  left < 0
                    ? `expired ${shortDay(d.valid_until, today)}`
                    : `valid until ${shortDay(d.valid_until, today)}`
                }
              >
                {d.number && (
                  <span>
                    Number <span className="num">{d.number}</span>
                  </span>
                )}
                {d.note && <span>{d.note}</span>}
                <span className="k-actions">
                  <button type="button" className="k-link" onClick={() => setEditing(d)}>
                    Edit
                  </button>
                  <button
                    type="button"
                    className="k-link k-error-text"
                    onClick={() => setDeleting(d)}
                  >
                    Delete
                  </button>
                </span>
              </Row>
            );
          })}
        </Group>
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.holder}'s ${DOCUMENT_KIND[deleting.kind].toLowerCase()}?`}
          description="Trips stop checking it. This cannot be undone."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id)}
          onClose={() => setDeleting(null)}
        />
      )}
      {editing && (
        <DocumentSheet
          document={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
    </TravelFrame>
  );
}
