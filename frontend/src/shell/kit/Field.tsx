import type { ReactNode } from 'react';

/** A label above its control; one column on a phone. */
export function Field({
  label,
  htmlFor,
  hint,
  error,
  children,
}: {
  label: string;
  htmlFor: string;
  hint?: string;
  error?: string;
  children: ReactNode;
}) {
  return (
    <div className="k-field">
      <label htmlFor={htmlFor}>{label}</label>
      {children}
      {hint && !error && <p className="k-hint">{hint}</p>}
      {error && (
        <p className="k-error" role="alert">
          {error}
        </p>
      )}
    </div>
  );
}

/** Related numbers side by side (two or three per line). */
export function FieldRow({ children, cols = 2 }: { children: ReactNode; cols?: 2 | 3 }) {
  return <div className={cols === 3 ? 'k-fields k-fields-3' : 'k-fields'}>{children}</div>;
}
