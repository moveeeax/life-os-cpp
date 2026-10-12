const input = document.getElementById('origin');
const saved = document.getElementById('saved');

chrome.storage.sync.get({ origin: 'https://life-os.tarassov.me' }, ({ origin }) => {
  input.value = origin;
});

document.getElementById('save').addEventListener('click', () => {
  const origin = input.value.trim().replace(/\/+$/, '');
  if (!/^https?:\/\/[^/]+$/.test(origin)) {
    saved.hidden = false;
    saved.textContent = 'Use an address like https://life-os.example.com';
    return;
  }
  chrome.storage.sync.set({ origin }, () => {
    saved.hidden = false;
    saved.textContent = 'Saved';
  });
});
