import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useState } from 'react';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api, apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { checkFile, storageMessage, type ParentKind, type PrivateFile } from '@/lib/files';
import { putWithProgress } from '@/lib/now/upload';

const BASE = '/api/v1/files';

/** The ready files of one medical record or travel document. */
export function useFiles(kind: ParentKind, parentId: string) {
  return useQuery({
    queryKey: qk.files.of(kind, parentId),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: PrivateFile[] }>(BASE, {
          query: { parent_kind: kind, parent_id: parentId },
          signal,
        })
      ).data,
  });
}

/** The writes a file changes: its parent's list, and the counts on the records. */
function writes(kind: ParentKind) {
  return kind === 'medical_record' ? [qk.files.all(), qk.medical.records()] : [qk.files.all()];
}

export function useDeleteFile(kind: ParentKind) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: writes(kind),
  });
}

/**
 * Opens a file: asks for a fresh two-minute link and goes there. The window
 * is opened first, inside the click, so a popup blocker lets it through.
 */
export function useOpenFile() {
  const [error, setError] = useState<string | null>(null);
  const open = async (id: string) => {
    setError(null);
    const tab = window.open('', '_blank');
    try {
      const { data } = await api.getJson<{ data: { url: string } }>(`${BASE}/${id}/download`);
      if (tab) {
        tab.opener = null;
        tab.location.href = data.url;
      } else {
        window.location.assign(data.url);
      }
    } catch (e) {
      tab?.close();
      setError(storageMessage(e) ?? apiErrorMessage(e, 'The file did not open.'));
    }
  };
  return { open, error };
}

/**
 * The upload of one file: the row and its presigned PUT, the bytes straight
 * to the storage, then /ready. A failed step removes the row it made.
 */
export function useUploadFile(kind: ParentKind, parentId: string) {
  const qc = useQueryClient();
  const [progress, setProgress] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);

  const upload = async (file: File): Promise<boolean> => {
    setError(null);
    const check = checkFile(file);
    if (!check.ok) {
      setError(check.reason);
      return false;
    }
    let id: string | null = null;
    try {
      setProgress(0);
      const created = (
        await api.postJson<{
          data: { file: PrivateFile; upload: { url: string; headers: Record<string, string> } };
        }>(BASE, {
          body: {
            parent_kind: kind,
            parent_id: parentId,
            name: file.name.slice(0, 200),
            content_type: check.type,
            bytes: file.size,
          },
        })
      ).data;
      id = created.file.id;
      try {
        await putWithProgress(created.upload.url, file, created.upload.headers, setProgress);
      } catch {
        throw new Error('The storage refused the file.');
      }
      await api.postJson<{ data: PrivateFile }>(`${BASE}/${id}/ready`);
      id = null;
      return true;
    } catch (e) {
      setError(
        storageMessage(e) ??
          (e instanceof Error && !('status' in e)
            ? e.message
            : apiErrorMessage(e, 'The upload failed.')),
      );
      if (id !== null) {
        // The pending row would only wait for the sweep; take it out now.
        await api.deleteJson<unknown>(`${BASE}/${id}`).catch(() => undefined);
      }
      return false;
    } finally {
      setProgress(null);
      await Promise.all(writes(kind).map((queryKey) => qc.invalidateQueries({ queryKey })));
    }
  };

  return { upload, progress, error, busy: progress !== null };
}
