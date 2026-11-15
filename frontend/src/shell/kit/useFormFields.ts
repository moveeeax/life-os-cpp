import { useState } from 'react';

/** What an input, a select or a textarea hands its onChange. */
export type FieldChange = { target: { value: string } };

/**
 * The state of a sheet form: the fields, `set(key)` for an input's onChange
 * and the raw setter for the fields that are not plain text.
 */
export function useFormFields<F extends object>(initial: F) {
  const [f, setF] = useState<F>(initial);
  const set = (k: keyof F) => (e: FieldChange) => setF({ ...f, [k]: e.target.value });
  return [f, set, setF] as const;
}
