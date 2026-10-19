import { useRef, useState } from 'react';

import { ConfirmDialog } from '@/components/ConfirmDialog';
import { useDeleteFile, useFiles, useOpenFile, useUploadFile } from '@/hooks/useFiles';
import { useErrorToast } from '@/hooks/useErrorToast';
import { apiErrorMessage } from '@/lib/api/client';
import { FILE_ACCEPT, sizeLabel, type ParentKind, type PrivateFile } from '@/lib/files';

import './attachments.css';

/**
 * The scans of one medical record or travel document, inside its open row:
 * a line per file (open, delete) and "Attach a scan". A file opens through a
 * fresh two-minute link; nothing here keeps a URL.
 */
export function Attachments({ kind, parentId }: { kind: ParentKind; parentId: string }) {
  const files = useFiles(kind, parentId);
  const upload = useUploadFile(kind, parentId);
  const opener = useOpenFile();
  const [deleting, setDeleting] = useState<PrivateFile | null>(null);
  const remove = useDeleteFile(kind);
  useErrorToast(remove.error);
  const input = useRef<HTMLInputElement>(null);
  const list = files.data ?? [];

  return (
    <div className="k-files">
      {files.isPending ? (
        <span className="k-muted">Files…</span>
      ) : files.isError ? (
        <span className="k-error-text">
          {apiErrorMessage(files.error, 'The files did not load.')}
        </span>
      ) : (
        list.map((f) => (
          <div key={f.id} className="k-kv">
            <button
              type="button"
              className="k-link k-file-name"
              data-num-ok
              onClick={() => void opener.open(f.id)}
            >
              {f.name}
            </button>
            <span className="k-actions">
              <span className="k-muted" data-num-ok>
                {sizeLabel(f.bytes)}
              </span>
              <button type="button" className="k-link k-error-text" onClick={() => setDeleting(f)}>
                Remove
              </button>
            </span>
          </div>
        ))
      )}
      <span className="k-actions">
        <button
          type="button"
          className="k-link"
          disabled={upload.busy}
          onClick={() => input.current?.click()}
        >
          {upload.busy ? (
            <span data-num-ok>{`Uploading… ${Math.round((upload.progress ?? 0) * 100)}%`}</span>
          ) : (
            'Attach a scan'
          )}
        </button>
        <input
          ref={input}
          type="file"
          accept={FILE_ACCEPT}
          hidden
          aria-label="Attach a scan"
          onChange={(e) => {
            const file = e.target.files?.[0];
            e.target.value = '';
            if (file) void upload.upload(file);
          }}
        />
      </span>
      {(upload.error ?? opener.error) && (
        <p role="alert" className="k-error">
          {upload.error ?? opener.error}
        </p>
      )}
      {deleting && (
        <ConfirmDialog
          title={`Remove ${deleting.name}?`}
          description="The file is deleted from the storage. This cannot be undone."
          confirmLabel="Remove"
          destructive
          busy={remove.isPending}
          onConfirm={() => remove.mutate(deleting.id, { onSuccess: () => setDeleting(null) })}
          onClose={() => setDeleting(null)}
        />
      )}
    </div>
  );
}
