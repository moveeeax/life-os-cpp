Now and the glass shell: every screen now sits over one picture, a random
wallpaper from the shared library (a video on a desktop, an image on a
phone), with a glass pill nav (Now, Tasks, Goals, Money, Food, Body,
Training) and an account menu instead of the sidebar. `/now` is the new
landing screen: the clock, a greeting by name, the day's one focus (a task,
a goal or typed text; ticking a task closes it) and up to three quiet lines
when something is off (late tasks, a goal behind, protein left in the
evening or a month above usual spend). Sections not redesigned yet sit on one
glass sheet. "Hide numbers" blurs amounts for a shared screen. Admins upload
wallpapers on `/admin/wallpapers` straight to the bucket, with a poster frame
taken in the browser. With the now module off, Now is left out and the app
opens on the first section. The frontend chart's `csp.storageHosts` lets the
bucket through the CSP (`docs/wallpapers.md`). A Chrome extension in
`tools/newtab-extension` opens Now in every new tab.
