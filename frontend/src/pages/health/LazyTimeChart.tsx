import { lazy, Suspense, type ComponentProps } from 'react';

import type { TimeChart as Chart } from './TimeChart';

// The chart library is the largest chunk of the app; a page that draws a chart
// renders its text first and pulls the library while the slot stays reserved.
const Impl = lazy(() => import('./TimeChart').then((m) => ({ default: m.TimeChart })));

/** TimeChart, loaded on first use. */
export function TimeChart(props: ComponentProps<typeof Chart>) {
  return (
    <Suspense fallback={<div aria-busy="true" style={{ minHeight: 240 }} />}>
      <Impl {...props} />
    </Suspense>
  );
}
