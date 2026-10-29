import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';

/** The site's privacy policy: public, linked from the footer and Google's consent screen. */

const CONTACT = 'michael.tarassov@gmail.com';

export const LIMITED_USE =
  "Life OS's use and transfer of information received from Google APIs adheres to the Google API Services User Data Policy, including the Limited Use requirements.";

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="space-y-2">
      <h2 className="text-base font-semibold text-foreground">{title}</h2>
      {children}
    </section>
  );
}

export function PrivacyPage() {
  return (
    <div className="container mx-auto max-w-3xl py-8">
      <Card>
        <CardHeader>
          <CardTitle>Privacy policy</CardTitle>
          <p className="text-sm text-muted-foreground">
            Life OS, life-os.tarassov.me · updated October 29, 2026
          </p>
        </CardHeader>
        <CardContent className="space-y-6 text-sm leading-relaxed text-muted-foreground">
          <p>
            Life OS is a personal system run by Michael Tarassov for a small number of invited
            people. It keeps what you put into it so that you can see your own day, tasks, goals,
            habits, health, money and travel in one place. It sells nothing, shows no ads and shares
            your data with no one.
          </p>
          <Section title="What it keeps">
            <p>
              Your account (email, name, a password hash) and what you enter or connect: tasks,
              notes, goals, habits, food, money records, trips, workouts, and the fitness data of a
              Mi account you link. Each user sees only their own data. Data is stored in a database
              and a file store on servers the owner runs, and in backups of them.
            </p>
          </Section>
          <Section title="Google Calendar">
            <p>
              When you connect Google Calendar, Life OS asks Google for access to your calendar so
              that your events appear on the Now page and Ani, the assistant on that page, can tell
              you about your day and free time. It reads your events when a page or Ani needs them
              and does not keep copies of them. It creates, moves or deletes an event only when you
              do it yourself or confirm a change Ani proposes. The access token is kept on the
              server, encrypted, and is deleted and revoked at Google when you press Disconnect.
            </p>
            <p>{LIMITED_USE}</p>
          </Section>
          <Section title="Telegram">
            <p>
              The owner's own Telegram account can be linked for Ani. New messages to that account
              are read once a minute; their text is kept only inside the note Ani gets about them
              and her conversation, and is not stored otherwise. Messages are sent or edited only
              after the owner confirms them.
            </p>
          </Section>
          <Section title="Ani">
            <p>
              Ani's replies are written by a language model of OpenAI, reached through the server.
              What you ask her and the parts of your data a question needs are sent to it for that
              answer. Facts Ani keeps about you are listed in Account, where you can change or
              delete them.
            </p>
          </Section>
          <Section title="Removing your data">
            <p>
              You can disconnect Google Calendar, Telegram or a Mi account in Account at any time.
              To delete your account and everything in it, write to the address below; it is done
              within 30 days, and backups age out after that.
            </p>
          </Section>
          <Section title="Contact">
            <p>
              <a className="underline" href={`mailto:${CONTACT}`}>
                {CONTACT}
              </a>
            </p>
          </Section>
        </CardContent>
      </Card>
    </div>
  );
}
