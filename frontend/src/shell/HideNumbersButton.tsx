import { useHideNumbers } from '@/hooks/useHideNumbers';

export function HideNumbersButton() {
  const [hidden, setHidden] = useHideNumbers();
  return (
    <button
      type="button"
      className="g-pill g-eye"
      aria-pressed={hidden}
      onClick={() => setHidden(!hidden)}
    >
      {hidden ? 'Show numbers' : 'Hide numbers'}
    </button>
  );
}
