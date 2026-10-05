// Class strings shared by the Workout pages (TailAdmin form and button looks).
import { cn } from '@/lib/utils';

import { cardClass } from '../health/ChartCard';

export { cardClass };

export const inputClass =
  'h-11 w-full rounded-lg border border-gray-300 bg-transparent px-3 text-sm text-gray-800 shadow-theme-xs placeholder:text-gray-400 focus:border-brand-300 focus:ring-3 focus:ring-brand-500/20 focus:outline-hidden aria-invalid:border-error-500 dark:border-gray-700 dark:bg-gray-900 dark:text-white/90';
export const textareaClass = cn(inputClass, 'h-auto py-2.5');
export const labelClass = 'mb-1.5 block text-theme-sm font-medium text-gray-700 dark:text-gray-400';
export const primaryButton =
  'inline-flex items-center justify-center gap-2 rounded-lg bg-brand-500 px-4 py-3 text-sm font-medium text-white shadow-theme-xs transition hover:bg-brand-600 disabled:cursor-not-allowed disabled:bg-brand-300';
export const secondaryButton =
  'inline-flex items-center justify-center gap-2 rounded-lg border border-gray-300 px-4 py-3 text-sm font-medium text-gray-700 transition hover:bg-gray-100 disabled:cursor-not-allowed disabled:opacity-50 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5';
export const dangerButton =
  'inline-flex items-center justify-center gap-2 rounded-lg border border-error-500 px-4 py-3 text-sm font-medium text-error-500 transition hover:bg-error-50 disabled:cursor-not-allowed disabled:opacity-50 dark:hover:bg-error-500/10';
export const iconButton =
  'inline-flex size-10 items-center justify-center rounded-lg border border-gray-300 text-gray-600 transition hover:bg-gray-100 disabled:cursor-not-allowed disabled:opacity-40 dark:border-gray-700 dark:text-gray-300 dark:hover:bg-white/5';
/** Panel of a modal opened from a shell page. */
export const modalPanel =
  'rounded-2xl bg-white p-5 font-outfit text-gray-800 sm:p-6 dark:bg-gray-900 dark:text-white/90';
