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
 * require-corp added. No effect on pages already served with those
 * headers — the script short-circuits if crossOriginIsolated is already
 * true (so our local serve.py path stays direct, no SW round-trip).
 */
(() => {
    // Running inside the service worker: install the fetch handler.
    if (typeof self !== 'undefined' && !('window' in self) && 'skipWaiting' in self) {
        self.addEventListener('install', () => self.skipWaiting());
        self.addEventListener('activate', (ev) => ev.waitUntil(self.clients.claim()));
        self.addEventListener('fetch', (ev) => {
            // `only-if-cached` + non-same-origin: left alone so the browser
            // handles the cache policy without our intervention.
            if (ev.request.cache === 'only-if-cached' && ev.request.mode !== 'same-origin') return;
            ev.respondWith(
                fetch(ev.request)
                    .then((r) => {
                        if (r.status === 0) return r;  // opaque response; leave as-is
                        const h = new Headers(r.headers);
                        h.set('Cross-Origin-Embedder-Policy', 'require-corp');
                        h.set('Cross-Origin-Opener-Policy',   'same-origin');
                        // Allow this origin to embed itself cross-isolated.
                        h.set('Cross-Origin-Resource-Policy', 'same-site');
                        return new Response(r.body, { status: r.status, statusText: r.statusText, headers: h });
                    })
                    .catch((e) => { console.error(e); return Response.error(); }),
            );
        });
        return;
    }

    // Running on the page: register the SW if not already isolated.
    if (typeof window === 'undefined') return;
    if (window.crossOriginIsolated)    return;   // already isolated, nothing to do
    if (!window.isSecureContext)       return;   // SW requires HTTPS or localhost
    if (!('serviceWorker' in navigator)) return; // no SW support → nothing we can do

    navigator.serviceWorker.register(document.currentScript.src, { scope: './' })
        .then((reg) => {
            // Reload so the freshly-installed SW becomes the controller and
            // the page's subsequent resource requests pick up the headers.
            if (!navigator.serviceWorker.controller) {
                window.location.reload();
            }
        })
        .catch((err) => { console.warn('coi-serviceworker: register failed', err); });
})();
