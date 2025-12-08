const CACHE = 'quarzlampe-pwa-v2';
const PRE_CACHE = ['./', './index.html', './manifest.webmanifest', './icon-lamp.svg'];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE).then((cache) => cache.addAll(PRE_CACHE)).catch(() => null),
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;

  const url = new URL(event.request.url);
  const sameOrigin = url.origin === self.location.origin;

  event.respondWith(
    caches.match(event.request).then((cached) => {
      const fetchAndCache = fetch(event.request)
        .then((resp) => {
          if (sameOrigin && resp && resp.status === 200 && resp.type === 'basic') {
            const clone = resp.clone();
            caches.open(CACHE).then((cache) => cache.put(event.request, clone));
          }
          return resp;
        })
        .catch(() => cached);

      return cached || fetchAndCache;
    }),
  );
});
