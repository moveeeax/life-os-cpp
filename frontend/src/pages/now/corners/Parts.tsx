import type { LinePart } from '@/lib/now';

/** A line whose numbers wear `.num`, so Hide numbers blurs them. */
export function Parts({ parts }: { parts: LinePart[] }) {
  return (
    <>
      {parts.map((p, i) =>
        p.num ? (
          <span key={i} className="num">
            {p.text}
          </span>
        ) : p.title ? (
          <span key={i} data-num-ok>
            {p.text}
          </span>
        ) : (
          <span key={i}>{p.text}</span>
        ),
      )}
    </>
  );
}

/** A meter row: label, a bar filled to share (0..1), a value. */
export function Meter({
  label,
  share,
  value,
  tone,
}: {
  label: string;
  share: number;
  value: React.ReactNode;
  tone?: 'warn' | 'ok';
}) {
  const width = `${Math.round(Math.max(0, Math.min(1, share)) * 100)}%`;
  return (
    <div className="c-meter">
      <span className="c-l" data-num-ok>
        {label}
      </span>
      <span className="c-track">
        <span className={tone ? `c-${tone}` : undefined} style={{ width }} />
      </span>
      <span className="c-val">{value}</span>
    </div>
  );
}
