import { Link } from 'react-router';

import { Card, CardContent, CardHeader } from '@/components/ui/card';

/**
 * The public home page of Life OS: what it is and does, what it does with
 * Google Calendar, and the way to the privacy policy and to sign in. It is
 * the "Home page" of the Google OAuth consent screen, so it needs no login.
 */

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="space-y-2">
      <h2 className="text-base font-semibold text-foreground">{title}</h2>
      {children}
    </section>
  );
}

export function AboutPage() {
  return (
    <div className="container mx-auto max-w-3xl py-8">
      <Card>
        <CardHeader>
          <h1 className="text-2xl font-semibold">Life OS</h1>
          <p className="text-sm text-muted-foreground">
            A personal life dashboard at life-os.tarassov.me, run by Michael Tarassov for a small
            number of invited people.
          </p>
        </CardHeader>
        <CardContent className="space-y-6 text-sm leading-relaxed text-muted-foreground">
          <p>
            Life OS keeps your day in one place: tasks, notes, goals, habits, food, workouts, sleep
            and activity from a linked Mi band, money across currencies, trips and the days spent in
            each country. Its Now screen shows the time, today's focus and a ribbon of the day. Ani,
            an assistant on that screen, answers questions about your own records and proposes
            changes that you confirm with a button.
          </p>
          <Section title="Google Calendar">
            <p>
              When you connect your Google Calendar in Account, Life OS reads the events of your
              primary calendar to draw them on the day ribbon of the Now screen and to find free
              time for a focus block; Ani reads them to answer about your day. It adds, moves or
              removes an event only when you press a button for it: your own Add, or your Confirm on
              a change Ani proposed. You can disconnect at any time; the access is then revoked and
              the stored token deleted.
            </p>
          </Section>
          <Section title="Your data">
            <p>
              Each person sees only their own data. Nothing is sold, no ads are shown and nothing is
              shared. The details are in the privacy policy.
            </p>
          </Section>
          <div className="flex flex-wrap gap-4 text-foreground">
            <Link to="/privacy" className="underline">
              Privacy policy
            </Link>
            <Link to="/" className="underline">
              Sign in
            </Link>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
