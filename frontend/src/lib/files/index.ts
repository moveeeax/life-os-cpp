// The private file store of medical records and travel documents: what it
// takes, and the words for what the storage answered.
import type { components } from '@/lib/api/schema.gen';
import { ApiClientError } from '@/lib/api/client';

type S = components['schemas'];
export type PrivateFile = S['PrivateFile'];
export type PrivateFileInput = S['PrivateFileInput'];
export type ParentKind = PrivateFile['parent_kind'];

const MB = 1024 * 1024;
export const MAX_FILE_BYTES = 25 * MB;
export const FILE_TYPES = [
  'application/pdf',
  'image/jpeg',
  'image/png',
  'image/webp',
  'image/heic',
] as const;
export type FileType = (typeof FILE_TYPES)[number];

/** The `accept` of the file input. */
export const FILE_ACCEPT = [...FILE_TYPES, '.heic'].join(',');

/** A browser may give a HEIC photo no type at all; the name tells. */
export function typeOf(file: { type: string; name: string }): FileType | null {
  if ((FILE_TYPES as readonly string[]).includes(file.type)) return file.type as FileType;
  if (file.type === '' && /\.heic$/i.test(file.name)) return 'image/heic';
  return null;
}

export function checkFile(file: {
  type: string;
  name: string;
  size: number;
}): { ok: true; type: FileType } | { ok: false; reason: string } {
  const type = typeOf(file);
  if (type === null) return { ok: false, reason: 'Use a PDF or a photo (JPEG, PNG, WebP, HEIC).' };
  if (file.size <= 0) return { ok: false, reason: 'The file is empty.' };
  if (file.size > MAX_FILE_BYTES) return { ok: false, reason: 'A file can be at most 25 MB.' };
  return { ok: true, type };
}

/** "1.2 MB", "340 KB". */
export function sizeLabel(bytes: number): string {
  if (bytes >= MB) return `${(bytes / MB).toFixed(1)} MB`;
  return `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

/** What to tell the person when storage said no. */
export function storageMessage(error: unknown): string | null {
  if (!(error instanceof ApiClientError)) return null;
  if (error.status !== 503) return null;
  if (error.code === 'storage_not_configured') return 'File storage is not set up on this server.';
  if (error.code === 'storage_public')
    return 'The storage would let anyone read this file, so it was not kept. The owner of the server has to fix the bucket policy.';
  return 'The file storage refused or did not answer. Try again later.';
}
