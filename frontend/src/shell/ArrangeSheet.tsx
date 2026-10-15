import { useState, type FormEvent } from 'react';

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
  const [corners, setCorners] = useState<NowCorners>(() =>
    cleanCorners(initialCorners ?? settings.corners),
  );
  // A chip of a module switched off is not offered; Tasks has its own corner.
  const habitsOn = useHabitsEnabled(true);
  const offered = DOCK_CHIPS.filter((c) => c !== 'habits' || habitsOn);
  const [links, setLinks] = useState<NowLink[]>(() => settings.links ?? []);
  const editLink = (i: number, patch: Partial<NowLink>) =>
    setLinks(links.map((l, j) => (j === i ? { ...l, ...patch } : l)));
  // A row with a URL that is neither a path nor http(s) blocks Save and says so.
  const badUrl = (l: NowLink) => l.url.trim() !== '' && linkKind(l.url.trim()) === null;
  const linksOk = links.every((l) => !badUrl(l));
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
    const clean = cleanLinks(links);
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
        {links.map((l, i) => (
          <div key={i} className="k-fields">
            <label className="k-field">
              <span>Title</span>
              <input
                maxLength={40}
                value={l.title}
                onChange={(e) => editLink(i, { title: e.target.value })}
              />
            </label>
            <label className="k-field">
              <span>URL or path</span>
              <input
                maxLength={2048}
                placeholder="/money/summary"
                value={l.url}
                onChange={(e) => editLink(i, { url: e.target.value })}
              />
            </label>
            {badUrl(l) && <p className="k-error">A path like /money or an http(s) address.</p>}
            <button
              type="button"
              className="k-btn"
              aria-label={`Remove ${l.title.trim() || 'link'}`}
              onClick={() => setLinks(links.filter((_, j) => j !== i))}
            >
              Remove
            </button>
          </div>
        ))}
        {links.length < LINKS_MAX && (
          <button
            type="button"
            className="k-btn"
            onClick={() => setLinks([...links, { title: '', url: '' }])}
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
