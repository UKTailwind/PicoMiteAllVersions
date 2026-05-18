const CAPS_PATH = '/__picomite_proxy/caps';
const WS_PATH = '/__picomite_proxy/ws';
const PROTOCOL_VERSION = 1;

function sameOriginUrl(path) {
    return new URL(path, window.location.href);
}

function websocketUrl(path) {
    const url = sameOriginUrl(path);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    return url;
}

function withTimeout(ms) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), ms);
    return { controller, timer };
}

async function fetchCaps(timeoutMs) {
    const { controller, timer } = withTimeout(timeoutMs);
    try {
        const res = await fetch(sameOriginUrl(CAPS_PATH), {
            cache: 'no-store',
            credentials: 'same-origin',
            signal: controller.signal,
        });
        if (!res.ok) return { ok: false, reason: `caps_http_${res.status}` };
        const caps = await res.json();
        if (!caps || caps.protocol !== 'picomite-wasm-proxy') {
            return { ok: false, reason: 'caps_protocol' };
        }
        if (caps.protocol_version !== PROTOCOL_VERSION) {
            return { ok: false, reason: 'caps_version', caps };
        }
        return { ok: true, caps };
    } catch (e) {
        return { ok: false, reason: e?.name === 'AbortError' ? 'caps_timeout' : 'caps_absent' };
    } finally {
        clearTimeout(timer);
    }
}

function openHelloWebSocket(timeoutMs) {
    return new Promise((resolve) => {
        let settled = false;
        let ws = null;
        const finish = (result) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            if (ws && !result.ok) {
                try { ws.close(); } catch (_) {}
            }
            resolve(result);
        };
        const timer = setTimeout(() => finish({ ok: false, reason: 'ws_timeout' }), timeoutMs);

        try {
            ws = new WebSocket(websocketUrl(WS_PATH));
        } catch (e) {
            finish({ ok: false, reason: 'ws_create' });
            return;
        }

        ws.onopen = () => {
            ws.send(JSON.stringify({
                type: 'hello',
                protocol_version: PROTOCOL_VERSION,
                requested_features: [],
            }));
        };
        ws.onerror = () => finish({ ok: false, reason: 'ws_error' });
        ws.onclose = () => finish({ ok: false, reason: 'ws_closed' });
        ws.onmessage = (ev) => {
            try {
                const caps = JSON.parse(String(ev.data));
                if (caps.type === 'caps' &&
                    caps.protocol === 'picomite-wasm-proxy' &&
                    caps.protocol_version === PROTOCOL_VERSION) {
                    finish({ ok: true, caps, ws });
                } else {
                    finish({ ok: false, reason: 'ws_caps_protocol' });
                }
            } catch (_) {
                finish({ ok: false, reason: 'ws_caps_parse' });
            }
        };
    });
}

export async function detectPicomiteProxy({ timeoutMs = 1200 } = {}) {
    const capsResult = await fetchCaps(timeoutMs);
    if (!capsResult.ok) {
        return {
            mode: 'static',
            online: false,
            caps: capsResult.caps || null,
            reason: capsResult.reason,
            ws: null,
        };
    }

    const wsResult = await openHelloWebSocket(timeoutMs);
    if (!wsResult.ok) {
        return {
            mode: 'static',
            online: false,
            caps: capsResult.caps,
            reason: wsResult.reason,
            ws: null,
        };
    }

    return {
        mode: 'proxy',
        online: true,
        caps: wsResult.caps,
        reason: 'ok',
        ws: wsResult.ws,
    };
}
