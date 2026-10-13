import { useState } from 'react';

import {
  useAddRuleTemplate,
  useDeleteRule,
  useRules,
  useSaveTravelSettings,
  useTravelSettings,
} from '@/hooks/useTravel';
import { useErrorToast } from '@/hooks/useErrorToast';
import { countryName, type TravelRule } from '@/lib/travel';
import { Group, Row } from '@/shell/kit';

import { LoadError, Placeholder, TravelFrame } from './frame';
import { RuleSheet } from './Sheets';

const WINDOW: Record<TravelRule['window_kind'], (r: TravelRule) => string> = {
  rolling_days: (r) => `any ${r.window_days} days`,
  rolling_12_months_in_year: () => 'any 12 months ending this year',
  since_last_entry: () => 'since the last entry',
};

/** The counter rules and the home country. */
export function TravelRulesPage() {
  const rules = useRules();
  const settings = useTravelSettings();
  const saveHome = useSaveTravelSettings();
  const template = useAddRuleTemplate();
  const remove = useDeleteRule();
  useErrorToast(saveHome.error ?? template.error ?? remove.error);
  const [editing, setEditing] = useState<TravelRule | 'new' | null>(null);
  const [home, setHome] = useState<string | null>(null);
  const homeValue = home ?? settings.data?.home_country ?? '';
  return (
    <TravelFrame
      title="Rules"
      headline="Day counters"
      sub="each counts your days in some countries against a threshold"
      actions={
        <>
          <button type="button" className="k-btn" onClick={() => template.mutate('schengen')}>
            <span data-num-ok>Add Schengen 90/180</span>
          </button>
          <button type="button" className="k-btn" onClick={() => template.mutate('kz_tax')}>
            Add Kazakhstan tax residency
          </button>
          <span className="k-grow" />
          <button type="button" className="k-btn k-solid" onClick={() => setEditing('new')}>
            New rule
          </button>
        </>
      }
    >
      <Group label="Home">
        <form
          className="k-inline-form"
          onSubmit={(e) => {
            e.preventDefault();
            if (homeValue) saveHome.mutate(homeValue.trim().toUpperCase());
          }}
        >
          <input
            className="k-input"
            maxLength={2}
            aria-label="Home country code"
            placeholder="KZ"
            value={homeValue}
            onChange={(e) => setHome(e.target.value)}
          />
          <button type="submit" className="k-btn" disabled={saveHome.isPending}>
            Save
          </button>
          <span className="k-muted">
            {settings.data?.home_country
              ? `Days before your first crossing are in ${countryName(settings.data.home_country)}.`
              : 'Where your days are before the first crossing.'}
          </span>
        </form>
      </Group>
      {rules.isPending ? (
        <Placeholder />
      ) : rules.isError ? (
        <LoadError error={rules.error} onRetry={() => void rules.refetch()} />
      ) : (
        <Group label="Rules">
          {rules.data.length === 0 ? (
            <p className="k-empty">No rules yet: add a template or your own.</p>
          ) : (
            rules.data.map((r) => (
              <Row
                key={r.id}
                title={r.name}
                sub={
                  <span
                    data-num-ok
                  >{`${r.countries.map((c) => (c === 'SCHENGEN' ? 'Schengen' : countryName(c))).join(', ')} · ${WINDOW[r.window_kind](r)} · ${r.threshold_kind === 'at_most' ? 'at most' : 'over'} ${r.threshold_days}`}</span>
                }
              >
                {r.source && <span data-num-ok>{r.source}</span>}
                <span className="k-actions">
                  <button type="button" className="k-link" onClick={() => setEditing(r)}>
                    Edit
                  </button>
                  <button
                    type="button"
                    className="k-link k-error-text"
                    onClick={() => remove.mutate(r.id)}
                  >
                    Delete
                  </button>
                </span>
              </Row>
            ))
          )}
        </Group>
      )}
      {editing && (
        <RuleSheet
          rule={editing === 'new' ? undefined : editing}
          onClose={() => setEditing(null)}
        />
      )}
    </TravelFrame>
  );
}
