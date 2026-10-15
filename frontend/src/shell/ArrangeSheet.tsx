import { useRef, useState, type FormEvent } from 'react';

import { useHabitsEnabled } from '@/hooks/useHabits';
import { useSaveNowSettings } from '@/hooks/useNow';
import {
  CHIP_LABEL,
  CORNER_KEYS,
  DOCK_CHIPS,
  DOCK_LABEL,
  LINKS_MAX,
  cleanCorners,
  cleanLinks,
  linkKind,
  type Chip,
  type NowCorners,
  type NowLink,
  type NowSettings,
} from '@/lib/now';
import { Sheet } from '@/shell/kit';

type LinkRow = NowLink & { id: number };

/** The weather city, the order of the dock of Now and the quick links of its Links corner. */
export function ArrangeSheet({
  settings,
  initialCorners,
  onClose,
}: {
  settings: NowSettings;
  /** A layout to start from instead of the saved one (Habits' "Show on Now"). */
  initialCorners?: NowCorners;
  onClose: () => void;
}) {
  const [city, setCity] = useState(settings.city ?? '');
  // A chip an older or newer client saved, unknown here, is offered as an empty corner.
  // Tasks has its own corner now: a slot an older layout gave it reads as empty.
  const [corners, setCorners] = useState<NowCorners>(() => {
    const c = cleanCorners(initialCorners ?? settings.corners);
    for (const k of CORNER_KEYS) if (c[k] === 'tasks') c[k] = null;
    return c;
  });
  // A chip of a module switched off is not offered; Tasks has its own corner.
  const habitsOn = useHabitsEnabled(true);
  const offered = DOCK_CHIPS.filter((c) => c !== 'habits' || habitsOn);
  // Rows carry an id of their own, so removing one keeps the others' fields in place.
  const nextId = useRef(0);
  const row = (l: NowLink): LinkRow => ({ id: nextId.current++, ...l });
  const [links, setLinks] = useState<LinkRow[]>(() => (settings.links ?? []).map(row));
  const editLink = (id: number, patch: Partial<NowLink>) =>
    setLinks(links.map((l) => (l.id === id ? { ...l, ...patch } : l)));
  // A row with a URL that is neither a path nor http(s), or with one of its two
  // fields empty, blocks Save and says so; a row with both empty is dropped.
  const badUrl = (l: NowLink) => l.url.trim() !== '' && linkKind(l.url.trim()) === null;
  const halfRow = (l: NowLink) => (l.title.trim() === '') !== (l.url.trim() === '');
  const linksOk = links.every((l) => !badUrl(l) && !halfRow(l));
  const save = useSaveNowSettings(onClose);
  // Picking a chip already in another corner moves it: that corner turns empty.
  const pick = (corner: (typeof CORNER_KEYS)[number], value: string) => {
    const chip = value === '' ? null : (value as Chip['key']);
    const next = { ...corners };
    for (const k of CORNER_KEYS) if (chip && next[k] === chip) next[k] = null;
    next[corner] = chip;
    setCorners(next);
  };
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!linksOk) return;
    const typed = city.trim();
    const cityChanged = typed !== (settings.city ?? '');
    const clean = cleanLinks(links.map(({ title, url }) => ({ title, url })));
    const linksChanged = JSON.stringify(clean) !== JSON.stringify(settings.links ?? []);
    save.mutate({
      corners,
      ...(cityChanged ? { city: typed === '' ? null : typed } : {}),
      ...(linksChanged ? { links: clean } : {}),
    });
  };
  return (
    <Sheet title="Arrange Now" onClose={onClose} width={440}>
      <form className="k-form" onSubmit={submit}>
        <label className="k-field">
          <span>Weather city</span>
          <input
            maxLength={120}
            placeholder="Almaty"
            value={city}
            onChange={(e) => setCity(e.target.value)}
          />
        </label>
        <p className="k-sub">Leave it empty to show no weather.</p>
        <div className="k-fields">
          {CORNER_KEYS.map((k) => (
            <label key={k} className="k-field">
              <span>{DOCK_LABEL[k]}</span>
              <select value={corners[k] ?? ''} onChange={(e) => pick(k, e.target.value)}>
                <option value="">Nothing</option>
                {offered.map((c) => (
                  <option key={c} value={c}>
                    {CHIP_LABEL[c]}
                  </option>
                ))}
              </select>
            </label>
          ))}
        </div>
        <p className="k-sub">Links: up to eight places you open from the Links corner.</p>
        {links.map((l) => (
          <div key={l.id} className="k-fields">
            <label className="k-field">
              <span>Title</span>
              <input
                maxLength={40}
                value={l.title}
                onChange={(e) => editLink(l.id, { title: e.target.value })}
              />
            </label>
            <label className="k-field">
              <span>URL or path</span>
              <input
                maxLength={2048}
                placeholder="/money/summary"
                value={l.url}
                onChange={(e) => editLink(l.id, { url: e.target.value })}
              />
            </label>
            {badUrl(l) && <p className="k-error">A path like /money or an http(s) address.</p>}
            {!badUrl(l) && halfRow(l) && (
              <p className="k-error">A title and a URL, or an empty row.</p>
            )}
            <button
              type="button"
              className="k-btn"
              aria-label={`Remove ${l.title.trim() || 'link'}`}
              onClick={() => setLinks(links.filter((x) => x.id !== l.id))}
            >
              Remove
            </button>
          </div>
        ))}
        {links.length < LINKS_MAX && (
          <button
            type="button"
            className="k-btn"
            onClick={() => setLinks([...links, row({ title: '', url: '' })])}
          >
            Add a link
          </button>
        )}
        {save.error && (
          <p role="alert" className="k-error">
            {save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={save.isPending || !linksOk}>
            {save.isPending ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}
