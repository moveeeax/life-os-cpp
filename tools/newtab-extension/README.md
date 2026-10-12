# Life OS Now in every new tab

A tiny Chrome extension (Manifest V3): every new tab redirects to `/now` of
your Life OS, so the clock, the focus and the quiet lines are the first thing
a new tab shows. The extension holds nothing but the address.

## Install

1. Open `chrome://extensions` and turn on "Developer mode".
2. "Load unpacked" and pick this folder (`tools/newtab-extension`).
3. If Life OS lives elsewhere than `https://life-os.tarassov.me`, set the
   address in the extension's options.

Incognito windows keep Chrome's own new tab: extensions cannot override it
there.

You need to be signed in to Life OS in the same browser profile; otherwise
the tab lands on the sign-in page and returns to Now after it.

The address bar shows the Life OS address, not an empty new tab: the
extension redirects instead of embedding the page, because the app forbids
framing (`frame-ancestors 'none'`).
