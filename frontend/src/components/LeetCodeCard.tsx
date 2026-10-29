import { useQuery } from '@tanstack/react-query';
import { useEffect, useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

type Profile = components['schemas']['LeetCodeProfile'];
const PATH = '/api/v1/leetcode/profile';

/**
 * The LeetCode username whose solved counts are pulled every hour; a
 * habit takes "LeetCode solved" as its source to tick itself.
 */
export function LeetCodeCard() {
  const profile = useQuery({
    queryKey: qk.leetcode.profile(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Profile | null }>(PATH, { signal })).data,
  });
  const save = useApiMutation(
    async (username: string) =>
      (
        await api.putJson<{ data: Profile }>(PATH, {
          body: { username, tz: Intl.DateTimeFormat().resolvedOptions().timeZone },
        })
      ).data,
    { invalidate: [qk.leetcode.profile()] },
  );
  const [name, setName] = useState('');
  useEffect(() => {
    if (profile.data?.username) setName(profile.data.username);
  }, [profile.data?.username]);
  const p = profile.data;

  return (
    <Card>
      <CardHeader>
        <CardTitle>LeetCode</CardTitle>
        <CardDescription>
          Your public profile, pulled every hour; a habit with the source “LeetCode solved” ticks
          itself. Counting starts the day after you link it: the first pull is the baseline.
        </CardDescription>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2">
          <Input
            aria-label="LeetCode username"
            placeholder="username"
            maxLength={30}
            value={name}
            onChange={(e) => setName(e.target.value.trim())}
            className="max-w-xs"
          />
          <Button
            variant="outline"
            disabled={save.isPending || !name}
            onClick={() => save.mutate(name)}
          >
            Save
          </Button>
        </div>
        {p?.total != null ? (
          <p className="num text-sm text-muted-foreground">
            {p.total} solved: {p.easy} easy, {p.medium} medium, {p.hard} hard
          </p>
        ) : null}
        {p?.last_error ? (
          <Alert variant="destructive">
            <AlertDescription>The last pull failed: {p.last_error}</AlertDescription>
          </Alert>
        ) : null}
        {save.error ? (
          <Alert variant="destructive">
            <AlertDescription>{save.error}</AlertDescription>
          </Alert>
        ) : null}
      </CardContent>
    </Card>
  );
}
