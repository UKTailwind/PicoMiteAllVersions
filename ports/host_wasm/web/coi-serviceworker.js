/*
 * coi-serviceworker: synthesize cross-origin-isolated context for pages
 * served from a host that can't set COOP/COEP response headers (e.g.
 * GitHub Pages). Adapted from https://github.com/gzuidhof/coi-serviceworker
 * (MIT). Needed because SharedArrayBuffer — which our wasm pthread build
 * relies on — requires the page to be cross-origin isolated.
 *
 * How it works: on first load, this script registers itself as a service
 * worker and reloads the page. On subsequent loads the service worker is
 * the controller, intercepts every fetch, and re-serves responses with
 * Cross-Origin-Opener-Policy: same-origin + Cross-Origin-Embedder-Policy:
 * require-corp added. It also keeps a versioned app-shell cache so the
 * web build is installable as a PWA and old release assets are evicted
 * when APP_CACHE_VERSION changes.
 */
(() => {
    const APP_SHELL = [
        './',
        './index.html',
        './manifest.webmanifest',
        './icon.svg',
        './icon-192.png',
        './icon-512.png',
        './apple-touch-icon.png',
        './safari-pinned-tab.svg',
        './picomite.css',
        './app.mjs',
        './app_version.mjs',
        './proxy_client.mjs',
        './worker.mjs',
        './ui/audio.js',
        './vendor/codemirror.js',
        './picomite.mjs',
        './picomite.wasm',
        './picomite.data',
    ];

    function isolationHeaders(response) {
        if (response.status === 0) return response;  // opaque response; leave as-is
        const headers = new Headers(response.headers);
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Resource-Policy', 'same-site');
        return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers,
        });
    }

    function isSameOrigin(requestUrl) {
        return requestUrl.origin === self.location.origin;
    }

    function cacheKeyFor(requestUrl) {
        const normalized = new URL(requestUrl.href);
        normalized.hash = '';
        return normalized.href;
    }

    // Running inside the service worker: install the fetch handler.
    if (typeof self !== 'undefined' && !('window' in self) && 'skipWaiting' in self) {
        const url = new URL(self.location.href);
        const cacheVersion = url.searchParams.get('version') || 'mmba-local';
        const cacheName = `${cacheVersion}-app-shell`;
        const appShellUrls = new Set(APP_SHELL.map((path) => cacheKeyFor(new URL(path, self.location.href))));

        self.addEventListener('install', (ev) => {
            ev.waitUntil(
                caches.open(cacheName)
                    .then((cache) => cache.addAll(APP_SHELL.map((path) => new Request(path, { cache: 'reload' }))))
                    .then(() => self.skipWaiting()),
            );
        });

        self.addEventListener('activate', (ev) => {
            ev.waitUntil(
                caches.keys()
                    .then((keys) => Promise.all(keys
                        .filter((key) => key.startsWith('mmba-') && key !== cacheName)
                        .map((key) => caches.delete(key))))
                    .then(() => self.clients.claim()),
            );
        });

        self.addEventListener('fetch', (ev) => {
            // `only-if-cached` + non-same-origin: left alone so the browser
            // handles the cache policy without our intervention.
            if (ev.request.cache === 'only-if-cached' && ev.request.mode !== 'same-origin') return;

            const requestUrl = new URL(ev.request.url);
            const cacheableAppShellRequest = ev.request.method === 'GET' &&
                isSameOrigin(requestUrl) &&
                appShellUrls.has(cacheKeyFor(requestUrl));

            if (cacheableAppShellRequest) {
                ev.respondWith(
                    caches.open(cacheName)
                        .then((cache) => fetch(new Request(ev.request, { cache: 'reload' }))
                            .then((response) => {
                                if (response.ok) cache.put(ev.request, response.clone());
                                return isolationHeaders(response);
                            })
                            .catch(() => cache.match(ev.request)
                                .then((cached) => {
                                    if (cached) return isolationHeaders(cached);
                                    return Response.error();
                                }))),
                );
                return;
            }

            ev.respondWith(
                fetch(ev.request)
                    .then((r) => isolationHeaders(r))
                    .catch((e) => { console.error(e); return Response.error(); }),
            );
        });
        return;
    }

    // Running on the page: register the SW. Static hosts such as GitHub
    // Pages need it for COOP/COEP and app-shell caching; local dev and
    // ngrok usually get real headers from serve.py and skip the SW below.
    if (typeof window === 'undefined') return;
    if (!window.isSecureContext)       return;   // SW requires HTTPS or localhost
    if (!('serviceWorker' in navigator)) return; // no SW support → nothing we can do

    const currentScriptUrl = document.currentScript.src;
    const alreadyIsolated = window.crossOriginIsolated;
    const devOrTunnelHost = (
        window.location.hostname === 'localhost' ||
        window.location.hostname === '127.0.0.1' ||
        window.location.hostname.endsWith('.ngrok-free.dev')
    );

    // Local dev servers and ngrok already send real COOP/COEP headers.
    // Keeping an app-shell SW active there can serve a stale worker/wasm
    // set during rapid rebuilds and leave the page stuck at "Booting
    // worker...". Use the SW only on static hosts that need header
    // synthesis, such as GitHub Pages.
    if (alreadyIsolated && devOrTunnelHost) {
        navigator.serviceWorker.getRegistrations()
            .then((regs) => Promise.all(regs.map((reg) => reg.unregister())))
            .then((removed) => {
                if (navigator.serviceWorker.controller && removed.some(Boolean)) {
                    window.location.reload();
                }
            })
            .catch((err) => { console.warn('coi-serviceworker: unregister failed', err); });
        return;
    }

    import('./app_version.mjs')
        .then(({ APP_CACHE_VERSION }) => {
            const serviceWorkerUrl = new URL(currentScriptUrl);
            serviceWorkerUrl.searchParams.set('version', APP_CACHE_VERSION);
            return navigator.serviceWorker.register(serviceWorkerUrl, { scope: './' });
        })
        .then(() => {
            // Reload so the freshly-installed SW becomes the controller and
            // the page's subsequent resource requests pick up the headers.
            if (!navigator.serviceWorker.controller && !alreadyIsolated) {
                window.location.reload();
            }
        })
        .catch((err) => { console.warn('coi-serviceworker: register failed', err); });
})();
