import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type CalendarStatus = components['schemas']['CalendarStatus'];

const PATH = '/api/v1/calendar';

/** GET /calendar: whether Google Calendar is linked, and to which account. */
export function useCalendarStatus() {
  return useQuery({
    queryKey: qk.calendar(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: CalendarStatus }>(PATH, { signal })).data,
  });
}

/** POST /calendar/connect: the URL of Google's consent, handed to @p onUrl. */
export function useConnectCalendar(onUrl: (url: string) => void) {
  return useApiMutation(
    async () => (await api.postJson<{ data: { url: string } }>(`${PATH}/connect`)).data,
    { onSuccess: (d) => onUrl(d.url) },
  );
}

/** DELETE /calendar: revoke and drop the link. */
export function useDisconnectCalendar() {
  return useApiMutation(async () => await api.deleteJson(PATH), {
    invalidate: [qk.calendar()],
  });
}
