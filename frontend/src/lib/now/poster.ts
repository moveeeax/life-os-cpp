// A still frame of a video, taken in the browser at upload time: the Now
// screen paints it at once and the video fades in over it.

const MAX_WIDTH = 1920;
const QUALITY = 0.8;

export function posterFrom(file: File): Promise<{ blob: Blob; width: number; height: number }> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const video = document.createElement('video');
    video.muted = true;
    video.preload = 'auto';
    video.src = url;
    const fail = () => {
      URL.revokeObjectURL(url);
      reject(new Error('This video cannot be read by the browser.'));
    };
    video.onerror = fail;
    video.onloadedmetadata = () => {
      video.currentTime = Math.min(1, video.duration / 2 || 0);
    };
    video.onseeked = () => {
      const width = video.videoWidth;
      const height = video.videoHeight;
      const scale = Math.min(1, MAX_WIDTH / width);
      const canvas = document.createElement('canvas');
      canvas.width = Math.round(width * scale);
      canvas.height = Math.round(height * scale);
      const ctx = canvas.getContext('2d');
      if (!ctx) return fail();
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      canvas.toBlob(
        (blob) => {
          URL.revokeObjectURL(url);
          if (blob) resolve({ blob, width, height });
          else reject(new Error('The poster could not be made.'));
        },
        'image/jpeg',
        QUALITY,
      );
    };
  });
}
