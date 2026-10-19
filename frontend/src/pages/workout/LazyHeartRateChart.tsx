import { lazy, Suspense, type ComponentProps } from 'react';

import type { HeartRateChart as Chart } from './HeartRateChart';

// The chart library is the largest chunk of the app: the workout renders
// first, the chart follows into a reserved slot.
const Impl = lazy(() => import('./HeartRateChart').then((m) => ({ default: m.HeartRateChart })));

/** HeartRateChart, loaded on first use. */
export function HeartRateChart(props: ComponentProps<typeof Chart>) {
  return (
    <Suspense fallback={<div aria-busy="true" style={{ minHeight: 240 }} />}>
      <Impl {...props} />
    </Suspense>
  );
}
