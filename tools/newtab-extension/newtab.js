// Every new tab goes to Life OS Now. The origin is set on the options page.
const DEFAULT_ORIGIN = 'https://life-os.tarassov.me';

chrome.storage.sync.get({ origin: DEFAULT_ORIGIN }, ({ origin }) => {
  location.replace(String(origin || DEFAULT_ORIGIN).replace(/\/+$/, '') + '/now');
});
