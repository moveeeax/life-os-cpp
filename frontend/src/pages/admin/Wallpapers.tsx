import { useState, type FormEvent } from 'react';
import { Link } from 'react-router';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import {
  useAdminWallpapers,
  useCreateWallpaper,
  useDeleteWallpaper,
  useWallpaperReady,
} from '@/hooks/useNow';
import { apiErrorMessage } from '@/lib/api/client';
import type { AdminWallpaper } from '@/lib/now';
import { LIMITS, checkFile, putWithProgress } from '@/lib/now/upload';
import { posterFrom } from '@/lib/now/poster';

const MB = 1024 * 1024;

function imageSize(file: File): Promise<{ width: number; height: number }> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => {
      URL.revokeObjectURL(url);
      resolve({ width: img.naturalWidth, height: img.naturalHeight });
    };
    img.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error('This image cannot be read by the browser.'));
    };
    img.src = url;
  });
}

/** The shared wallpaper library: upload a video or an image straight to the bucket. */
export function AdminWallpapersPage() {
  const list = useAdminWallpapers();
  const create = useCreateWallpaper();
  const ready = useWallpaperReady();
  const remove = useDeleteWallpaper();
  const [file, setFile] = useState<File | null>(null);
  const [device, setDevice] = useState<'desktop' | 'phone'>('desktop');
  const [progress, setProgress] = useState<number | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<AdminWallpaper | null>(null);
  // A new key empties the file input after an upload, so the same file can be picked again.
  const [inputKey, setInputKey] = useState(0);

  const upload = async (e: FormEvent) => {
    e.preventDefault();
    if (!file) return;
    setMessage(null);
    const check = checkFile(file, device);
    if (!check.ok) {
      setMessage(check.reason);
      return;
    }
    try {
      setProgress(0);
      const poster = check.kind === 'video' ? await posterFrom(file) : null;
      if (poster && poster.blob.size > LIMITS.poster) {
        throw new Error('The poster came out over 1 MB; try a smaller video.');
      }
      const size = poster ? { width: poster.width, height: poster.height } : await imageSize(file);
      const created = await create.mutateAsync({
        kind: check.kind,
        device,
        content_type: file.type as 'video/mp4' | 'video/webm' | 'image/jpeg' | 'image/webp',
        bytes: file.size,
        ...(poster ? { poster_bytes: poster.blob.size } : {}),
        width: size.width,
        height: size.height,
      });
      await putWithProgress(created.upload.url, file, created.upload.headers, (s) =>
        setProgress(s * 0.95),
      );
      if (poster && created.poster_upload) {
        await putWithProgress(
          created.poster_upload.url,
          poster.blob,
          created.poster_upload.headers,
          () => {},
        );
      }
      await ready.mutateAsync(created.id);
      setProgress(null);
      setFile(null);
      setInputKey((k) => k + 1);
      setMessage('Uploaded. It shows up in new tabs from now on.');
    } catch (err) {
      setProgress(null);
      const status = (err as { status?: number }).status;
      setMessage(
        status === 409
          ? 'The upload did not finish. Delete this row and try again.'
          : err instanceof Error
            ? err.message
            : apiErrorMessage(err),
      );
      list.refetch();
    }
  };

  const rows = list.data ?? [];

  return (
    <div className="container mx-auto max-w-4xl py-8 space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-3xl font-bold">Wallpapers</h1>
          <p className="text-sm text-muted-foreground">
            Shared by every user. A random one shows behind every screen; phones get images only.
          </p>
        </div>
        <Button asChild variant="ghost">
          <Link to="/admin">← Admin</Link>
        </Button>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Upload</CardTitle>
        </CardHeader>
        <CardContent>
          <form className="space-y-4" onSubmit={upload}>
            <div className="space-y-2">
              <Label htmlFor="wallpaper-file">File</Label>
              <input
                key={inputKey}
                id="wallpaper-file"
                type="file"
                accept="video/mp4,video/webm,image/jpeg,image/webp"
                className="block text-sm"
                onChange={(e) => setFile(e.target.files?.[0] ?? null)}
              />
              <p className="text-xs text-muted-foreground">
                Video MP4 or WebM up to 100 MB (no sound needed, it plays muted); image JPEG or WebP
                up to 10 MB.
              </p>
            </div>
            <fieldset className="space-y-2">
              <legend className="text-sm font-medium">For</legend>
              <div className="flex gap-4 text-sm">
                {(['desktop', 'phone'] as const).map((d) => (
                  <label key={d} className="flex items-center gap-2">
                    <input
                      type="radio"
                      name="device"
                      checked={device === d}
                      onChange={() => setDevice(d)}
                    />
                    {d === 'desktop' ? 'Desktop' : 'Phone'}
                  </label>
                ))}
              </div>
            </fieldset>
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
          <CardTitle>{list.isLoading ? 'Loading…' : `${rows.length} wallpaper(s)`}</CardTitle>
        </CardHeader>
        <CardContent>
          {list.error ? (
            <p className="text-sm text-destructive">{apiErrorMessage(list.error)}</p>
          ) : (
            <ul className="divide-y divide-border">
              {rows.map((w) => (
                <li key={w.id} className="flex items-center gap-4 py-3">
                  <img
                    src={w.poster_url ?? w.url}
                    alt=""
                    className="h-14 w-24 flex-none rounded-md object-cover bg-muted"
                  />
                  <div className="min-w-0 flex-1 text-sm">
                    <div className="font-medium">
                      {w.kind === 'video' ? 'Video' : 'Image'} ·{' '}
                      {w.device === 'desktop' ? 'Desktop' : 'Phone'}
                    </div>
                    <div className="text-muted-foreground">
                      {(w.bytes / MB).toFixed(1)} MB
                      {w.width && w.height ? ` · ${w.width}×${w.height}` : ''}
                      {w.status === 'pending' ? ' · not finished' : ''}
                    </div>
                  </div>
                  <Button variant="ghost" onClick={() => setDeleting(w)}>
                    Delete
                  </Button>
                </li>
              ))}
            </ul>
          )}
        </CardContent>
      </Card>

      {deleting && (
        <ConfirmDialog
          title="Delete this wallpaper?"
          description="It goes from the bucket and from every screen."
          confirmLabel="Delete"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id, { onSuccess: () => setDeleting(null) })}
          onClose={() => setDeleting(null)}
        />
      )}
    </div>
  );
}
