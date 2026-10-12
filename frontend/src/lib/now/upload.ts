// What the wallpaper library takes, and the browser's own upload to the
// bucket through a presigned PUT (the API never carries the bytes).

const MB = 1024 * 1024;

export const LIMITS = { video: 100 * MB, image: 10 * MB, poster: MB, sound: 30 * MB } as const;
export const TYPES = {
  video: ['video/mp4', 'video/webm'],
  image: ['image/jpeg', 'image/webp'],
} as const;

export const AUDIO_TYPES = [
  'audio/mpeg',
  'audio/mp4',
  'audio/x-m4a',
  'audio/aac',
  'audio/ogg',
] as const;

/** A focus sound: MP3, AAC/M4A or Ogg up to 30 MB. */
export function checkAudio(file: {
  type: string;
  size: number;
}): { ok: true } | { ok: false; reason: string } {
  if (!(AUDIO_TYPES as readonly string[]).includes(file.type))
    return { ok: false, reason: 'Use MP3, AAC/M4A or Ogg for a sound.' };
  if (file.size <= 0) return { ok: false, reason: 'The file is empty.' };
  if (file.size > LIMITS.sound) return { ok: false, reason: 'A sound can be at most 30 MB.' };
  return { ok: true };
}

export type CheckResult = { ok: true; kind: 'video' | 'image' } | { ok: false; reason: string };

export function checkFile(
  file: { type: string; size: number },
  device: 'desktop' | 'phone',
): CheckResult {
  const kind = (TYPES.video as readonly string[]).includes(file.type)
    ? 'video'
    : (TYPES.image as readonly string[]).includes(file.type)
      ? 'image'
      : null;
  if (kind === null)
    return { ok: false, reason: 'Use MP4 or WebM for a video, JPEG or WebP for an image.' };
  if (kind === 'video' && device === 'phone')
    return { ok: false, reason: 'A phone gets images only.' };
  if (file.size <= 0) return { ok: false, reason: 'The file is empty.' };
  if (file.size > LIMITS[kind]) {
    return { ok: false, reason: `A ${kind} can be at most ${LIMITS[kind] / MB} MB.` };
  }
  return { ok: true, kind };
}

/** PUT @p body to a presigned URL with exactly the signed headers; reports the share sent. */
export function putWithProgress(
  url: string,
  body: Blob,
  headers: Record<string, string>,
  onProgress: (share: number) => void,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('PUT', url);
    for (const [k, v] of Object.entries(headers)) xhr.setRequestHeader(k, v);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) onProgress(e.loaded / e.total);
    };
    xhr.onload = () =>
      xhr.status >= 200 && xhr.status < 300
        ? resolve()
        : reject(new Error(`The storage answered ${xhr.status}.`));
    xhr.onerror = () => reject(new Error('The upload did not reach the storage.'));
    xhr.send(body);
  });
}
