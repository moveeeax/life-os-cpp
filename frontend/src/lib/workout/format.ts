// Display formats shared by the session pages.

/** "Mon, Oct 5" in the browser's zone. */
export const formatDay = (iso: string): string =>
  new Date(iso).toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' });

/** "09:30" in the browser's zone. */
export const formatTime = (iso: string): string =>
  new Date(iso).toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });

/** 12345.6 -> "12,346 kg". */
export const formatVolume = (kg: number): string => `${Math.round(kg).toLocaleString('en-US')} kg`;

/** 63 -> "1 h 03 min", 45 -> "45 min". */
export function formatDuration(minutes: number | null): string {
  if (minutes === null) return '–';
  if (minutes < 60) return `${minutes} min`;
  return `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')} min`;
}
