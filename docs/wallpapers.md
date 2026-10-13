# Wallpapers and sounds: the bucket behind every screen

Life OS shows a random wallpaper behind every screen: a video on a desktop,
an image on a phone. The admin uploads them on `/admin/library` (the old
`/admin/wallpapers` address redirects there); the files
live in an S3-compatible bucket, the rows in the `wallpapers` table
(migration 031). The browser uploads straight to the bucket through a
presigned PUT, so the API never carries the bytes, and reads the files from a
public URL (the bucket or a CDN in front of it).

Until the settings below are filled, the wallpaper routes answer 503
`storage_not_configured` and the app paints a dark gradient.

## Settings

| Env | Value |
|---|---|
| `NOW_ENABLED` | `true` |
| `S3_ENDPOINT` | Origin of the store, e.g. `https://storage.example.com`, no trailing slash |
| `S3_REGION` | Region used in the signature |
| `S3_BUCKET` | Bucket name |
| `S3_PREFIX` | Key prefix, `life-os/` by default; objects go to `<prefix>wallpapers/<id>/` |
| `S3_ACCESS_KEY_ID`, `S3_SECRET_ACCESS_KEY` | Keys with write and delete on the prefix |
| `S3_PUBLIC_BASE_URL` | Origin the browser reads objects from; a file's URL is this plus its key |
| `S3_PATH_STYLE` | `true` for `endpoint/bucket/key` (MinIO and most S3-compatible stores), `false` for `bucket.endpoint-host/key` |

In the `life-os-cpp` chart these are `storage.s3.*` values; the two keys come
from a Secret named in `storage.s3.credentialsSecret.name` (keys
`access-key-id` and `secret-access-key` by default). The frontend chart needs
the read and upload origins in `csp.storageHosts`, otherwise the browser's CSP
blocks the pictures and the upload:

```yaml
csp:
  storageHosts:
    - https://cdn.example.com       # S3_PUBLIC_BASE_URL
    - https://storage.example.com   # S3_ENDPOINT (path style) or https://<bucket>.<endpoint host>
```

## Bucket

CORS, so the app's origin can PUT and anyone can read:

```json
[
  {
    "AllowedOrigins": ["https://life-os.tarassov.me"],
    "AllowedMethods": ["PUT"],
    "AllowedHeaders": ["content-type"],
    "MaxAgeSeconds": 3600
  },
  {
    "AllowedOrigins": ["*"],
    "AllowedMethods": ["GET", "HEAD"],
    "AllowedHeaders": [],
    "MaxAgeSeconds": 3600
  }
]
```

Public read on the wallpapers and sounds prefixes only. AWS-style policy
(stores differ: some take this policy, some only an ACL per bucket; check
yours):

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": "*",
      "Action": ["s3:GetObject"],
      "Resource": [
        "arn:aws:s3:::<bucket>/life-os/wallpapers/*",
        "arn:aws:s3:::<bucket>/life-os/sounds/*"
      ]
    }
  ]
}
```

The access key needs `s3:PutObject`, `s3:GetObject` (HEAD) and
`s3:DeleteObject` on `<bucket>/life-os/wallpapers/*` and
`<bucket>/life-os/sounds/*` and nothing else.

## Files

- Video: MP4 or WebM, up to 100 MB. It plays muted and looped; a few seconds
  that loop cleanly work best. Keep it light: every new tab downloads it
  (once, then from the browser cache).
- Image: JPEG or WebP, up to 10 MB. Mark it "Phone" for phones (portrait
  works best there) or "Desktop".
- The upload page takes a frame of a video as its poster (JPEG, up to 1920 px
  wide): the screen shows it at once and the video fades in over it. With
  "reduce motion" or data saving on, only the poster shows.

## Sounds

Focus sounds (rain, cafe, quiet music) live in the same bucket under
`<prefix>sounds/<id>/`, uploaded on `/admin/library`: MP3, AAC/M4A or Ogg, up
to 30 MB. The policy and key rights above already cover that prefix.

## Prod

Prod uses the cluster's MinIO at `https://s3.tarassov.me`: bucket `life-os`,
user `life-os` with the policy `life-os-rw` (put, get and delete on
`life-os/wallpapers/*` and `life-os/sounds/*`, list on the bucket), anonymous
download on the `life-os/wallpapers` and `life-os/sounds` prefixes, keys in the Secret `life-os-s3` of the `life-os`
namespace. MinIO answers CORS preflights for any origin by itself, so no bucket
CORS rule is set there. Region `us-east-1` (MinIO's default).

## Check

1. Upload one desktop video and one phone image on `/admin/library`.
2. Open `/now` on a desktop: the video plays behind the clock.
3. Open `/now` on a phone (or a window 768 px wide or less): the image shows.
4. A row stuck as "not finished" is an upload that did not complete; delete
   it and upload again.
