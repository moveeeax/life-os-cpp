import { useState, type FormEvent } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useAdminSounds, useCreateSound, useDeleteSound, useSoundReady } from '@/hooks/useNow';
import { apiErrorMessage } from '@/lib/api/client';
import type { AdminSound, SoundCreate } from '@/lib/now';
import { checkAudio, putWithProgress } from '@/lib/now/upload';

const MB = 1024 * 1024;

/** The length of an audio file in whole seconds, read by the browser; null when it cannot tell. */
function durationOf(file: File): Promise<number | null> {
  return new Promise((resolve) => {
    const url = URL.createObjectURL(file);
    const a = new Audio();
    a.preload = 'metadata';
    a.onloadedmetadata = () => {
      URL.revokeObjectURL(url);
      resolve(Number.isFinite(a.duration) && a.duration > 0 ? Math.round(a.duration) : null);
    };
    a.onerror = () => {
      URL.revokeObjectURL(url);
      resolve(null);
    };
    a.src = url;
  });
}

function minutes(seconds: number | null): string {
  if (!seconds) return '';
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return ` · ${m}:${String(s).padStart(2, '0')}`;
}

/** The focus sounds of the library: rain, cafe, quiet music, played in focus mode. */
export function SoundsSection() {
  const list = useAdminSounds();
  const create = useCreateSound();
  const ready = useSoundReady();
  const remove = useDeleteSound();
  const [file, setFile] = useState<File | null>(null);
  const [title, setTitle] = useState('');
  const [progress, setProgress] = useState<number | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<AdminSound | null>(null);
  const [inputKey, setInputKey] = useState(0);

  const pick = (f: File | null) => {
    setFile(f);
    if (f && !title) setTitle(f.name.replace(/\.[^.]+$/, '').slice(0, 80));
  };

  const upload = async (e: FormEvent) => {
    e.preventDefault();
    if (!file) return;
    setMessage(null);
    const check = checkAudio(file);
    if (!check.ok) {
      setMessage(check.reason);
      return;
    }
    if (!title.trim()) {
      setMessage('Name the sound.');
      return;
    }
    let createdId: string | null = null;
    try {
      setProgress(0);
      const seconds = await durationOf(file);
      const created = await create.mutateAsync({
        title: title.trim().slice(0, 80),
        content_type: file.type as SoundCreate['content_type'],
        bytes: file.size,
        ...(seconds ? { duration_seconds: seconds } : {}),
      });
      createdId = created.id;
      await putWithProgress(created.upload.url, file, created.upload.headers, setProgress);
      await ready.mutateAsync(created.id);
      setProgress(null);
      setFile(null);
      setTitle('');
      setInputKey((k) => k + 1);
      setMessage('Uploaded. It is in the sound list of focus mode now.');
    } catch (err) {
      setProgress(null);
      let cleaned = createdId === null;
      if (createdId !== null) {
        try {
          await remove.mutateAsync(createdId);
          cleaned = true;
        } catch {
          // Left as "not finished"; delete it from the list.
        }
      }
      const reason = err instanceof Error ? err.message : apiErrorMessage(err);
      setMessage(
        cleaned
          ? `The upload did not finish: ${reason} Try again.`
          : `The upload did not finish: ${reason} Delete the "not finished" row and try again.`,
      );
      list.refetch();
    }
  };

  const rows = list.data ?? [];
  return (
    <section className="space-y-6" aria-labelledby="library-sounds">
      <div>
        <h2 id="library-sounds" className="text-2xl font-semibold">
          Sounds
        </h2>
        <p className="text-sm text-muted-foreground">
          Played in a loop in focus mode: rain, a cafe, quiet music without words.
        </p>
      </div>
      <Card>
        <CardHeader>
          <CardTitle>Upload a sound</CardTitle>
        </CardHeader>
        <CardContent>
          <form className="space-y-4" onSubmit={upload}>
            <div className="space-y-2">
              <Label htmlFor="sound-file">File</Label>
              <input
                key={inputKey}
                id="sound-file"
                type="file"
                accept="audio/mpeg,audio/mp4,audio/x-m4a,audio/aac,audio/ogg,.mp3,.m4a,.ogg"
                className="block text-sm"
                onChange={(e) => pick(e.target.files?.[0] ?? null)}
              />
              <p className="text-xs text-muted-foreground">
                MP3, AAC/M4A or Ogg, up to 30 MB. A clean loop works best.
              </p>
            </div>
            <div className="space-y-2">
              <Label htmlFor="sound-title">Name</Label>
              <Input
                id="sound-title"
                value={title}
                maxLength={80}
                onChange={(e) => setTitle(e.target.value)}
              />
            </div>
            <div className="flex items-center gap-3">
              <Button type="submit" disabled={!file || progress !== null}>
                Upload
              </Button>
              {progress !== null && (
                <span className="text-sm text-muted-foreground">{Math.round(progress * 100)}%</span>
              )}
            </div>
            {message && (
              <p className="text-sm" role="status">
                {message}
              </p>
            )}
          </form>
        </CardContent>
      </Card>
      <Card>
        <CardHeader>
          <CardTitle>{list.isLoading ? 'Loading…' : `${rows.length} sound(s)`}</CardTitle>
        </CardHeader>
        <CardContent>
          {list.error ? (
            <p className="text-sm text-destructive">{apiErrorMessage(list.error)}</p>
          ) : (
            <ul className="divide-y divide-border">
              {rows.map((s) => (
                <li key={s.id} className="flex flex-wrap items-center gap-4 py-3">
                  <div className="min-w-0 flex-1 text-sm">
                    <div className="font-medium">{s.title}</div>
                    <div className="text-muted-foreground">
                      {(s.bytes / MB).toFixed(1)} MB{minutes(s.duration_seconds)}
                      {s.status === 'pending' ? ' · not finished' : ''}
                    </div>
                  </div>
                  {s.status === 'ready' && (
                    <audio controls preload="none" src={s.url} className="h-8 max-w-full" />
                  )}
                  <Button variant="ghost" onClick={() => setDeleting(s)}>
                    Delete
                  </Button>
                </li>
              ))}
            </ul>
          )}
          {remove.error && <p className="text-sm text-destructive">{remove.error}</p>}
        </CardContent>
      </Card>
      {deleting && (
        <ConfirmDialog
          title="Delete this sound?"
          description="It goes from the bucket and from focus mode."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id, { onSuccess: () => setDeleting(null) })}
          onClose={() => setDeleting(null)}
        />
      )}
    </section>
  );
}
