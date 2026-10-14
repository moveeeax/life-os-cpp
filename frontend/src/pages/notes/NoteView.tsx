import type { ReactNode } from 'react';

import { parseNote, type Span } from '@/lib/notes';

import './notes.css';

function Spans({ parts }: { parts: Span[] }) {
  return (
    <>
      {parts.map((p, i): ReactNode => {
        switch (p.kind) {
          case 'bold':
            return <strong key={i}>{p.text}</strong>;
          case 'italic':
            return <em key={i}>{p.text}</em>;
          case 'code':
            return <code key={i}>{p.text}</code>;
          case 'link':
            return (
              <a key={i} href={p.href} target="_blank" rel="noopener noreferrer" className="k-link">
                {p.text}
              </a>
            );
          default:
            return <span key={i}>{p.text}</span>;
        }
      })}
    </>
  );
}

/** A note body as rendered text; nothing in it is read as HTML. */
export function NoteView({ body }: { body: string }) {
  const blocks = parseNote(body);
  if (blocks.length === 0) return <p className="k-empty">Nothing written yet.</p>;
  return (
    <div className="n-prose" data-num-ok>
      {blocks.map((b, i) => {
        switch (b.kind) {
          case 'heading': {
            const H = `h${b.level + 1}` as 'h2' | 'h3' | 'h4';
            return (
              <H key={i}>
                <Spans parts={b.parts} />
              </H>
            );
          }
          case 'list':
            return (
              <ul key={i}>
                {b.items.map((it, j) => (
                  <li key={j}>
                    <Spans parts={it} />
                  </li>
                ))}
              </ul>
            );
          case 'olist':
            return (
              <ol key={i}>
                {b.items.map((it, j) => (
                  <li key={j}>
                    <Spans parts={it} />
                  </li>
                ))}
              </ol>
            );
          case 'quote':
            return (
              <blockquote key={i}>
                <Spans parts={b.parts} />
              </blockquote>
            );
          case 'code':
            return (
              <pre key={i}>
                <code>{b.text}</code>
              </pre>
            );
          default:
            return (
              <p key={i}>
                <Spans parts={b.parts} />
              </p>
            );
        }
      })}
    </div>
  );
}
