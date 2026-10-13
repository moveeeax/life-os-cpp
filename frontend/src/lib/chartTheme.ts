import type { ApexOptions } from 'apexcharts';

const INK = 'rgba(255, 255, 255, 0.56)';
const LINE = 'rgba(255, 255, 255, 0.12)';

/** A chart for the glass sheet: transparent ground, white-ish ink, dark tooltip, the caller's series and colours kept. */
export function glassChart(base: ApexOptions): ApexOptions {
  return {
    ...base,
    chart: {
      ...base.chart,
      background: 'transparent',
      foreColor: INK,
      fontFamily: 'Outfit Variable, Outfit, ui-sans-serif, system-ui, sans-serif',
      toolbar: { show: false, ...base.chart?.toolbar },
    },
    theme: { ...base.theme, mode: 'dark' },
    grid: { borderColor: LINE, strokeDashArray: 3, ...base.grid },
    tooltip: { ...base.tooltip, theme: 'dark' },
  };
}
