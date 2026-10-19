import { useMedicalOn } from '@/hooks/useMedical';
import type { TabItem } from '@/shell/kit';

/** Body's tabs: the band data, then the medical archive. */
export const BODY_TABS: TabItem[] = [
  { to: '/health', label: 'Band', end: true },
  { to: '/health/records', label: 'Records' },
  { to: '/health/visits', label: 'Visits' },
  { to: '/health/medicines', label: 'Medicines' },
  { to: '/health/family', label: 'Family' },
];

/** The tabs of Body while the medical module is on; none otherwise (the band page alone). */
export function useBodyTabs(): TabItem[] | undefined {
  return useMedicalOn() ? BODY_TABS : undefined;
}
