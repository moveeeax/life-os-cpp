import { useState } from 'react';
import { Dumbbell } from 'lucide-react';

import { mediaUrl } from '@/lib/workout';
import { cn } from '@/lib/utils';

interface ExercisePhotoProps {
  /** An `images` entry of the exercise; absent for a custom exercise. */
  path?: string;
  alt: string;
  className?: string;
}

/**
 * A photo from /exercise-media/, or a placeholder when the exercise has none
 * or the file is not served (the dev server has no photos).
 */
export function ExercisePhoto({ path, alt, className }: ExercisePhotoProps) {
  const [failedPath, setFailedPath] = useState<string | null>(null);
  if (!path || failedPath === path) {
    return (
      <div
        className={cn(
          'flex items-center justify-center bg-gray-100 text-gray-400 dark:bg-white/5 dark:text-gray-500',
          className,
        )}
        role="img"
        aria-label={`${alt}: no photo`}
      >
        <Dumbbell className="size-1/4 max-h-10 max-w-10" aria-hidden="true" />
      </div>
    );
  }
  return (
    <img
      src={mediaUrl(path)}
      alt={alt}
      loading="lazy"
      decoding="async"
      onError={() => setFailedPath(path)}
      className={cn('bg-gray-100 object-cover dark:bg-white/5', className)}
    />
  );
}
