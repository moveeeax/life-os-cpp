import Chart from 'react-apexcharts';
import type { ApexOptions } from 'apexcharts';

import { glassChart } from '@/lib/chartTheme';

interface HeartRateChartProps {
  samples: { timestamp: string; bpm: number }[];
  startedAt: string;
  finishedAt: string;
}

/** Heart rate inside one workout: a point per band sample, local time on the axis. */
export function HeartRateChart({ samples, startedAt, finishedAt }: HeartRateChartProps) {
  // The glass sheet is dark on every screen.
  const muted = 'rgba(255, 255, 255, 0.56)';

  const options: ApexOptions = glassChart({
    chart: {
      fontFamily: "'Outfit Variable', sans-serif",
      toolbar: { show: false },
      zoom: { enabled: false },
      background: 'transparent',
      animations: { enabled: false },
    },
    colors: ['#fca5a5'],
    stroke: { width: 2, curve: 'straight' },
    markers: { size: 4, strokeWidth: 0 },
    dataLabels: { enabled: false },
    grid: { borderColor: 'rgba(255, 255, 255, 0.12)', strokeDashArray: 4 },
    xaxis: {
      type: 'datetime',
      min: Date.parse(startedAt),
      max: Date.parse(finishedAt),
      axisBorder: { show: false },
      axisTicks: { show: false },
      labels: { datetimeUTC: false, format: 'HH:mm', style: { colors: muted } },
      tooltip: { enabled: false },
    },
    yaxis: {
      title: { text: 'bpm', style: { color: muted, fontWeight: 400 } },
      labels: { style: { colors: muted }, formatter: (v: number) => String(Math.round(v)) },
    },
    tooltip: { x: { format: 'HH:mm' }, y: { formatter: (v: number) => `${Math.round(v)} bpm` } },
  });

  return (
    <div className="num-chart">
      <Chart
        options={options}
        series={[
          {
            name: 'Heart rate',
            data: samples.map((s) => ({ x: Date.parse(s.timestamp), y: s.bpm })),
          },
        ]}
        type="line"
        height={240}
      />
    </div>
  );
}
