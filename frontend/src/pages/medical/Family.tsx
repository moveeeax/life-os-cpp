import { useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeletePerson, useDeleteProvider, usePeople, useProviders } from '@/hooks/useMedical';
import { useErrorToast } from '@/hooks/useErrorToast';
import type { MedicalPerson, MedicalProvider } from '@/lib/medical';
import { Group, Row } from '@/shell/kit';

import { LoadError, MedicalFrame, Placeholder } from './frame';
import { PersonSheet, ProviderSheet } from './Sheets';

/** The people the archive is about, and their doctors and clinics. */
export function MedicalFamilyPage() {
  const people = usePeople();
  const providers = useProviders();
  const [person, setPerson] = useState<MedicalPerson | 'new' | null>(null);
  const [provider, setProvider] = useState<MedicalProvider | 'new' | null>(null);
  const [deleting, setDeleting] = useState<
    { kind: 'person'; row: MedicalPerson } | { kind: 'provider'; row: MedicalProvider } | null
  >(null);
  const removePerson = useDeletePerson(() => setDeleting(null));
  const removeProvider = useDeleteProvider(() => setDeleting(null));
  useErrorToast(removePerson.error);
  useErrorToast(removeProvider.error);
  const actions = (onEdit: () => void, onDelete: () => void) => (
    <span className="k-actions">
      <button type="button" className="k-link" onClick={onEdit}>
        Edit
      </button>
      <button type="button" className="k-link k-error-text" onClick={onDelete}>
        Delete
      </button>
    </span>
  );

  return (
    <MedicalFrame
      headline="Family"
      sub="the people of the archive and their doctors"
      actions={
        <>
          <span className="k-grow" />
          <button type="button" className="k-btn" onClick={() => setProvider('new')}>
            New doctor or clinic
          </button>
          <button type="button" className="k-btn k-solid" onClick={() => setPerson('new')}>
            New person
          </button>
        </>
      }
    >
      {people.isPending || providers.isPending ? (
        <Placeholder />
      ) : people.isError ? (
        <LoadError error={people.error} onRetry={() => void people.refetch()} />
      ) : providers.isError ? (
        <LoadError error={providers.error} onRetry={() => void providers.refetch()} />
      ) : (
        <>
          <Group label="People">
            {people.data.length === 0 ? (
              <p className="k-empty">Nobody yet: add yourself and the family.</p>
            ) : (
              people.data.map((p) => (
                <Row key={p.id} title={p.name} sub={p.relation || undefined}>
                  {p.note && <span className="k-pre">{p.note}</span>}
                  {actions(
                    () => setPerson(p),
                    () => setDeleting({ kind: 'person', row: p }),
                  )}
                </Row>
              ))
            )}
          </Group>
          <Group label="Doctors and clinics">
            {providers.data.length === 0 ? (
              <p className="k-empty">No doctors or clinics yet.</p>
            ) : (
              providers.data.map((d) => (
                <Row
                  key={d.id}
                  title={d.name}
                  sub={
                    [d.kind === 'clinic' ? 'Clinic' : d.specialty || 'Doctor', d.clinic]
                      .filter(Boolean)
                      .join(' · ') || undefined
                  }
                >
                  {d.kind === 'clinic' && d.specialty && <span>{d.specialty}</span>}
                  {d.phone && (
                    <a className="k-link num" href={`tel:${d.phone.replace(/\s+/g, '')}`}>
                      {d.phone}
                    </a>
                  )}
                  {d.email && (
                    <a className="k-link" href={`mailto:${d.email}`}>
                      {d.email}
                    </a>
                  )}
                  {d.address && <span>{d.address}</span>}
                  {d.note && <span className="k-pre">{d.note}</span>}
                  {actions(
                    () => setProvider(d),
                    () => setDeleting({ kind: 'provider', row: d }),
                  )}
                </Row>
              ))
            )}
          </Group>
        </>
      )}
      {person && (
        <PersonSheet
          person={person === 'new' ? undefined : person}
          onClose={() => setPerson(null)}
        />
      )}
      {provider && (
        <ProviderSheet
          provider={provider === 'new' ? undefined : provider}
          onClose={() => setProvider(null)}
        />
      )}
      {deleting && (
        <ConfirmDialog
          title={`Delete ${deleting.row.name}?`}
          description={
            deleting.kind === 'person'
              ? 'Only a person with no visits, records or medications can go.'
              : 'Their visits stay, without the doctor.'
          }
          confirmLabel="Delete"
          destructive
          busy={removePerson.isPending || removeProvider.isPending}
          onConfirm={() =>
            deleting.kind === 'person'
              ? removePerson.mutate(deleting.row.id)
              : removeProvider.mutate(deleting.row.id)
          }
          onClose={() => setDeleting(null)}
        />
      )}
    </MedicalFrame>
  );
}
