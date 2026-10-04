import { useState } from 'react';

import { Alert, AlertDescription } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { useMe } from '@/hooks/useMe';
import { api } from '@/lib/api/client';

/**
 * Shown when the user is logged in but the access JWT carries
 * confirmed=false. Unconfirmed users are kept off non-account routes
 * and redirected to /unconfirmed.
 */
export function UnconfirmedPage() {
  const user = useMe().data ?? null;
  const [resent, setResent] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const resend = async () => {
    setError(null);
    const { error: e } = await api.POST('/api/v1/account/confirm-resend');
    if (e) {
      setError('Could not resend the confirmation email. Try again later.');
      return;
    }
    setResent(true);
  };

  return (
    <div className="container mx-auto max-w-md py-8">
      <Card>
        <CardHeader>
          <CardTitle>Confirm your email</CardTitle>
          <CardDescription>
            We sent a confirmation link to {user?.email ?? 'your email address'}. Click it to
            unlock the rest of the app.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {resent && (
            <Alert variant="success">
              <AlertDescription>A new confirmation link is on its way.</AlertDescription>
            </Alert>
          )}
          {error && (
            <Alert variant="destructive">
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}
          <Button onClick={resend} className="w-full" variant="outline">
            Resend confirmation email
          </Button>
        </CardContent>
      </Card>
    </div>
  );
}
