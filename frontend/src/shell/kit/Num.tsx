import type { ReactNode } from 'react';

import { formatAmount } from './logic';

/** Any number a section prints: "Hide numbers" blurs `.num`. */
export function Num({ children, className }: { children: ReactNode; className?: string }) {
  return <span className={className ? `num ${className}` : 'num'}>{children}</span>;
}

/** An amount: true minus, grouped digits, the currency code after it. */
export function Amount({
  value,
  currency,
  signed,
  digits = 2,
  className,
}: {
  value: number;
  currency?: string;
  signed?: boolean;
  digits?: number;
  className?: string;
}) {
  const tone = signed ? (value > 0 ? ' k-pos' : value < 0 ? ' k-neg' : '') : '';
  return (
    <span className={`num k-amount${tone}${className ? ` ${className}` : ''}`}>
      {formatAmount(value, { currency, signed, digits })}
    </span>
  );
}
