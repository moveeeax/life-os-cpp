import { useQueryClient } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { batchFailureText, runEach, type BatchOutcome } from '@/lib/batch';

/**
 * One mutation for a list of rows ("Post all", "Copy packing"): the rows are
 * sent one after another inside a single mutationFn, so there is one
 * isPending, one error ("2 of 5 failed: …") and one round of invalidation,
 * not one of each per row.
 */
export function useBatchMutation<T>(
  run: (item: T) => Promise<unknown>,
  options: {
    invalidate: readonly (readonly unknown[])[];
    onSuccess?: (outcome: BatchOutcome) => void;
  },
) {
  const qc = useQueryClient();
  return useApiMutation(
    async (items: T[]) => {
      const outcome = await runEach(items, run);
      const failure = batchFailureText(outcome);
      if (failure) {
        // The rows that went through are on the server: the lists must show
        // them even though the call as a whole reports the failure.
        await Promise.all(options.invalidate.map((queryKey) => qc.invalidateQueries({ queryKey })));
        throw new Error(failure);
      }
      return outcome;
    },
    { invalidate: options.invalidate, onSuccess: options.onSuccess },
  );
}
