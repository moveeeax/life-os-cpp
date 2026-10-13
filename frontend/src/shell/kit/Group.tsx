import type { ReactNode } from 'react';

/** A small uppercase label over a run of rows, with the group's total on the right. */
export function Group({
  label,
  total,
  tone,
  children,
}: {
  label: ReactNode;
  total?: ReactNode;
  tone?: 'late';
  children: ReactNode;
}) {
  return (
    <section className="k-group">
      <h3 className={tone === 'late' ? 'k-group-label k-late' : 'k-group-label'}>
        <span>{label}</span>
        {total !== undefined && <span>{total}</span>}
      </h3>
      <div className="k-group-rows">{children}</div>
    </section>
  );
}
