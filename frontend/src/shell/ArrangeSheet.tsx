import { useState, type FormEvent } from 'react';

import { useHabitsEnabled } from '@/hooks/useHabits';
import { useSaveNowSettings } from '@/hooks/useNow';
import {
  CHIP_LABEL,
  CORNER_KEYS,
  CORNER_LABEL,
  cleanCorners,
  type Chip,
  type NowCorners,
  type NowSettings,
} from '@/lib/now';
import { Sheet } from '@/shell/kit';

const CHIP_KEYS = Object.keys(CHIP_LABEL) as Chip['key'][];

/** The weather city and which chip sits in which corner of Now. */
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
  // A chip of a module switched off is not offered.
  const habitsOn = useHabitsEnabled(true);
  const offered = CHIP_KEYS.filter((c) => c !== 'habits' || habitsOn);
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
    const typed = city.trim();
    const cityChanged = typed !== (settings.city ?? '');
    save.mutate({
      corners,
      ...(cityChanged ? { city: typed === '' ? null : typed } : {}),
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
              <span>{CORNER_LABEL[k]}</span>
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
        {save.error && (
          <p role="alert" className="k-error">
            {save.error}
          </p>
        )}
        <div className="k-dlg-actions">
          <button type="button" className="k-btn" onClick={onClose}>
            Cancel
          </button>
          <button type="submit" className="k-btn k-solid" disabled={save.isPending}>
            {save.isPending ? 'Saving…' : 'Save'}
          </button>
        </div>
      </form>
    </Sheet>
  );
}
