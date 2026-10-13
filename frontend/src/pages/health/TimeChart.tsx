import Chart from 'react-apexcharts';
import type { ApexOptions } from 'apexcharts';

import { glassChart } from '@/lib/chartTheme';

export interface Series {
  name: string;
  type: 'column' | 'line' | 'area';
  /** One value per day of `days`; null where there is no data. */
  data: (number | null)[];
  /** Index of the y axis this series uses (0 = left, 1 = right). */
  axis?: 0 | 1;
  color: string;
}

interface TimeChartProps {
  days: string[];
  series: Series[];
  stacked?: boolean;
  /** Axis titles, left then right. */
  yTitles: [string] | [string, string];
  /** Fixed bounds per axis, left then right; an absent bound is picked by the chart. */
  yBounds?: { min?: number; max?: number }[];
  /**
   * Draw lines across days without a value (sparse data such as weigh-ins).
   * The x axis becomes a time axis so the spacing between points stays true.
   */
  connect?: boolean;
  /** Formats a value in the tooltip. */
  format?: (value: number, seriesIndex: number) => string;
  /** Label of a day on the x axis (category axis only); default "MM-DD". */
  xLabel?: (day: string) => string;
  height?: number;
}

/** A per-day chart: one x position per calendar day of the selected period. */
export function TimeChart({
  days,
  series,
  stacked,
  yTitles,
  yBounds,
  connect,
  format,
  xLabel,
  height = 300,
}: TimeChartProps) {
  // The glass shell is dark on every screen: the ink of the sheet.
  const muted = 'rgba(255, 255, 255, 0.56)';
  const grid = 'rgba(255, 255, 255, 0.12)';
  // Fewer date labels and a lower chart on a phone.
  const phone = typeof window !== 'undefined' && window.innerWidth < 640;
  const maxTicks = phone ? 5 : 10;
  const ticks = Math.max(1, Math.min(days.length - 1, maxTicks));

  const yaxis: ApexOptions['yaxis'] = yTitles.map((text, i) => ({
    seriesName: series.filter((s) => (s.axis ?? 0) === i).map((s) => s.name),
    opposite: i === 1,
    min: yBounds?.[i]?.min,
    max: yBounds?.[i]?.max,
    title: { text, style: { color: muted, fontWeight: 400 } },
    labels: {
      style: { colors: muted },
      formatter: (v: number) => (Number.isFinite(v) ? String(Math.round(v * 10) / 10) : ''),
    },
  }));

  const options: ApexOptions = glassChart({
    chart: {
      fontFamily: "'Outfit Variable', sans-serif",
      toolbar: { show: false },
      zoom: { enabled: false },
      stacked,
      background: 'transparent',
      animations: { enabled: false },
    },
    colors: series.map((s) => s.color),
    stroke: {
      width: series.map((s) => (s.type === 'column' ? 0 : 2)),
      curve: 'smooth',
    },
    fill: { opacity: series.map((s) => (s.type === 'area' ? 0.15 : 1)) },
    markers: { size: connect || days.length <= 31 ? 3 : 0, strokeWidth: 0 },
    plotOptions: { bar: { columnWidth: '60%', borderRadius: 2 } },
    dataLabels: { enabled: false },
    grid: { borderColor: grid, strokeDashArray: 4 },
    legend: { position: 'top', horizontalAlign: 'left', labels: { colors: muted } },
    xaxis: connect
      ? {
          type: 'datetime',
          tickAmount: ticks,
          min: Date.parse(`${days[0]}T00:00:00Z`),
          max: Date.parse(`${days[days.length - 1]}T00:00:00Z`),
          axisBorder: { show: false },
          axisTicks: { show: false },
          labels: { datetimeUTC: true, format: 'MM-dd', style: { colors: muted } },
          tooltip: { enabled: false },
        }
      : {
          categories: days,
          tickAmount: ticks,
          axisBorder: { show: false },
          axisTicks: { show: false },
          labels: {
            rotate: 0,
            hideOverlappingLabels: true,
            style: { colors: muted },
            // 2026-10-05 -> 10-05
            formatter: (v: string) =>
              typeof v === 'string' ? (xLabel ? xLabel(v) : v.slice(5)) : '',
          },
          tooltip: { enabled: false },
        },
    yaxis,
    tooltip: {
      shared: true,
      intersect: false,
      x: connect
        ? { format: 'yyyy-MM-dd' }
        : { formatter: (_v, opts) => days[opts?.dataPointIndex ?? 0] ?? '' },
      y: {
        formatter: (v: number | null, opts) =>
          v === null || v === undefined
            ? '–'
            : format
              ? format(v, opts?.seriesIndex ?? 0)
              : String(Math.round(v * 10) / 10),
      },
    },
    noData: { text: 'No data' },
  });

  return (
    <div className="num-chart">
      <Chart
        options={options}
        series={series.map((s) => ({
          name: s.name,
          type: s.type,
          data: connect
            ? s.data.flatMap((y, i) =>
                y === null ? [] : [{ x: Date.parse(`${days[i]}T00:00:00Z`), y }],
              )
            : s.data,
        }))}
        type="line"
        height={phone ? Math.min(height, 240) : height}
      />
    </div>
  );
}
