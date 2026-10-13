import { useId, useState, type ReactNode } from 'react';

interface RowProps {
  title: ReactNode;
  sub?: ReactNode;
  right?: ReactNode;
  /** A colour dot before the title. */
  dot?: string;
  /** A small picture before the title (40 px). */
  thumb?: string;
  /** Anything before the title that is its own control (a check button). */
  lead?: ReactNode;
  /** The details; absent means the row does not open. */
  children?: ReactNode;
  open?: boolean;
  onToggle?: (open: boolean) => void;
  id?: string;
}

/** One calm row: title and one value; the rest behind a click. */
export function Row({
  title,
  sub,
  right,
  dot,
  thumb,
  lead,
  children,
  open,
  onToggle,
  id,
}: RowProps) {
  const [own, setOwn] = useState(false);
  const isOpen = open ?? own;
  const detailsId = useId();
  const expandable = children !== undefined && children !== null && children !== false;
  const toggle = () => {
    const next = !isOpen;
    if (open === undefined) setOwn(next);
    onToggle?.(next);
  };
  const body = (
    <>
      <span className="k-row-top">
        {dot && <span className="k-dot" style={{ background: dot }} aria-hidden="true" />}
        {thumb && <img className="k-thumb" src={thumb} alt="" loading="lazy" />}
        <span className="k-row-title">{title}</span>
        {right !== undefined && <span className="k-row-right">{right}</span>}
      </span>
      {sub && <span className="k-row-sub">{sub}</span>}
    </>
  );
  return (
    <div className="k-row" id={id} data-open={isOpen ? '' : undefined}>
      <div className="k-row-line">
        {lead}
        {expandable ? (
          <button
            type="button"
            className="k-row-head"
            aria-expanded={isOpen}
            aria-controls={detailsId}
            onClick={toggle}
          >
            {body}
          </button>
        ) : (
          <div className="k-row-head">{body}</div>
        )}
      </div>
      {expandable && isOpen && (
        <div className="k-row-more" id={detailsId}>
          {children}
        </div>
      )}
    </div>
  );
}
