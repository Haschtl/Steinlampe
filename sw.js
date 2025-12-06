const CACHE = 'quarzlampe-v1';
const ASSETS = ['/', '/webble.html', '/manifest.json', '/icon-lamp.svg'];

self.addEventListener('install', (event) => {
  event.waitUntil(
    (async () => {
      const cache = await caches.open(CACHE);
      for (const url of ASSETS) {
        try {
          await cache.add(url);
        } catch (e) {
          // ignore missing assets so install doesn't fail
          console.warn('SW: cache miss', url, e);
        }
      }
    })()
  );
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    )
  );
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  event.respondWith(
    caches.match(event.request).then((resp) => resp || fetch(event.request))
  );
});
