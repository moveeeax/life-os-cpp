import type { ReactNode } from 'react';

/** Any number a section prints: "Hide numbers" blurs `.num`. */
export function Num({ children, className }: { children: ReactNode; className?: string }) {
  return <span className={className ? `num ${className}` : 'num'}>{children}</span>;
}
