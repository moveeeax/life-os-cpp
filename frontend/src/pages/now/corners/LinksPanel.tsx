import { Link } from 'react-router';

import { linkKind, type NowLink } from '@/lib/now';

/**
 * The Links corner: the user's quick links from Arrange. A path of this app
 * goes through the router; an http(s) URL opens a new tab; anything else is
 * shown as text and never linked.
 */
export function LinksPanel({ links }: { links: NowLink[] }) {
  return (
    <>
      <h2>Links</h2>
      {links.length === 0 && <p className="c-empty">No links yet. Add them in Arrange.</p>}
      {links.map((l, i) => {
        const kind = linkKind(l.url);
        if (kind === 'path') {
          return (
            <Link key={i} className="c-row c-plain c-link" to={l.url}>
              <span className="c-t" data-num-ok>
                {l.title}
              </span>
            </Link>
          );
        }
        if (kind === 'external') {
          return (
            <a
              key={i}
              className="c-row c-plain c-link"
              href={l.url}
              target="_blank"
              rel="noopener noreferrer"
            >
              <span className="c-t" data-num-ok>
                {l.title}
              </span>
              <span className="c-r" aria-label="opens in a new tab">
                ↗
              </span>
            </a>
          );
        }
        return (
          <div key={i} className="c-row c-plain">
            <span className="c-t" data-num-ok>
              {l.title}
            </span>
          </div>
        );
      })}
    </>
  );
}
