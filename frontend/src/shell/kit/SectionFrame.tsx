import type { ReactNode } from 'react';

import type { TabItem } from './logic';
import { Tabs } from './Tabs';

/** A section on the calm sheet: eyebrow, headline, one line under it, tabs, tools, then the page. */
export function SectionFrame({
  eyebrow,
  headline,
  sub,
  tabs,
  tools,
  children,
}: {
  eyebrow: string;
  headline: ReactNode;
  sub?: ReactNode;
  tabs?: TabItem[];
  tools?: ReactNode;
  children: ReactNode;
}) {
  return (
    <div className="g-col">
      <section className="k-sheet" aria-labelledby="k-section-title">
        <div className="k-eyebrow">{eyebrow}</div>
        <h1 className="k-headline" id="k-section-title">
          {headline}
        </h1>
        {sub && <div className="k-sub">{sub}</div>}
        {tabs && tabs.length > 0 && <Tabs items={tabs} label={`${eyebrow} pages`} />}
        {tools && <div className="k-tools">{tools}</div>}
        <div className="k-body">{children}</div>
      </section>
    </div>
  );
}
