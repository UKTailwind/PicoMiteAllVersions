/*
 * drivers/web_console/web_console_assets.c
 *
 * Target-clean static page used by device ports before full display/audio/input
 * integration is enabled.
 */

#include "web_console_assets.h"

#include <string.h>

static const char index_html[] =
"<!doctype html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"<title>MMBasic Web Console</title>\n"
"<link rel=\"stylesheet\" href=\"/__web_console/style.css\">\n"
"</head>\n"
"<body>\n"
"<main>\n"
"<canvas id=\"display\" width=\"320\" height=\"240\"></canvas>\n"
"<section id=\"panel\">\n"
"<h1>MMBasic Web Console</h1>\n"
"<div id=\"status\">connecting</div>\n"
"<pre id=\"log\"></pre>\n"
"</section>\n"
"</main>\n"
"<script src=\"/__web_console/app.js\"></script>\n"
"</body>\n"
"</html>\n";

static const char style_css[] =
"html,body{margin:0;min-height:100%;background:#101214;color:#d8dee9;"
"font:14px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}"
"body{display:grid;place-items:center}"
"main{display:grid;grid-template-columns:auto 320px;gap:24px;align-items:start}"
"#display{width:640px;height:480px;image-rendering:pixelated;background:#000;"
"border:1px solid #3a4048}"
"#panel{max-width:320px}"
"h1{font-size:18px;line-height:1.2;margin:0 0 12px}"
"#status{margin:0 0 12px;color:#e5c07b}"
"#status.ok{color:#98c379}"
"#status.err{color:#e06c75}"
"#log{white-space:pre-wrap;line-height:1.35;color:#abb2bf;margin:0}"
"@media(max-width:900px){body{place-items:start center;padding:16px}"
"main{grid-template-columns:1fr;gap:16px}#display{width:100%;height:auto;"
"max-width:640px}#panel{max-width:640px}}\n";

static const char app_js[] =
"(() => {\n"
"  const status = document.getElementById('status');\n"
"  const log = document.getElementById('log');\n"
"  const canvas = document.getElementById('display');\n"
"  const ctx = canvas.getContext('2d');\n"
"  let ws;\n"
"  function note(text, cls) {\n"
"    status.textContent = text;\n"
"    status.className = cls || '';\n"
"  }\n"
"  function append(text) {\n"
"    log.textContent = (new Date()).toLocaleTimeString() + ' ' + text + '\\n' + log.textContent;\n"
"  }\n"
"  function drawBinary(buf) {\n"
"    const view = new DataView(buf);\n"
"    if (buf.byteLength < 8) return;\n"
"    const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));\n"
"    const w = view.getUint16(4, true);\n"
"    const h = view.getUint16(6, true);\n"
"    if (w && h && (canvas.width !== w || canvas.height !== h)) {\n"
"      canvas.width = w;\n"
"      canvas.height = h;\n"
"    }\n"
"    if (magic === 'CMDS') {\n"
"      let p = 8;\n"
"      while (p < buf.byteLength) {\n"
"        const op = view.getUint8(p++);\n"
"        if (op === 1 && p + 4 <= buf.byteLength) {\n"
"          const rgb = view.getUint32(p, true); p += 4;\n"
"          ctx.fillStyle = '#' + rgb.toString(16).padStart(6, '0');\n"
"          ctx.fillRect(0, 0, canvas.width, canvas.height);\n"
"        } else break;\n"
"      }\n"
"      append('binary CMDS ' + w + 'x' + h);\n"
"    } else {\n"
"      append('binary ' + magic + ' ' + buf.byteLength + ' bytes');\n"
"    }\n"
"  }\n"
"  function connect() {\n"
"    const scheme = location.protocol === 'https:' ? 'wss://' : 'ws://';\n"
"    ws = new WebSocket(scheme + location.host + '/__web_console/ws');\n"
"    ws.binaryType = 'arraybuffer';\n"
"    ws.onopen = () => {\n"
"      note('connected', 'ok');\n"
"      append('socket open');\n"
"      ws.send(JSON.stringify({op:'hello', client:'browser', version:1}));\n"
"      ws.send(JSON.stringify({op:'status'}));\n"
"    };\n"
"    ws.onclose = () => {\n"
"      note('disconnected, retrying', 'err');\n"
"      setTimeout(connect, 1000);\n"
"    };\n"
"    ws.onerror = () => note('socket error', 'err');\n"
"    ws.onmessage = ev => {\n"
"      if (ev.data instanceof ArrayBuffer) { drawBinary(ev.data); return; }\n"
"      try {\n"
"        const msg = JSON.parse(ev.data);\n"
"        append((msg.op || 'json') + ' ' + ev.data);\n"
"        if (msg.op === 'hello' || msg.op === 'status') note('connected: ' + msg.op, 'ok');\n"
"      } catch (e) {\n"
"        append('text ' + ev.data);\n"
"      }\n"
"    };\n"
"  }\n"
"  window.addEventListener('keydown', ev => {\n"
"    if (!ws || ws.readyState !== WebSocket.OPEN) return;\n"
"    if (ev.key.length === 1) {\n"
"      ws.send(JSON.stringify({op:'key', code:ev.key.charCodeAt(0) & 255}));\n"
"      ev.preventDefault();\n"
"    } else if (ev.key === 'Enter') {\n"
"      ws.send(JSON.stringify({op:'key', code:13}));\n"
"      ev.preventDefault();\n"
"    }\n"
"  });\n"
"  note('connecting');\n"
"  connect();\n"
"})();\n";

static const web_console_asset_t assets[] = {
    { "/__web_console/", "text/html", index_html, sizeof(index_html) - 1u },
    { "/__web_console/index.html", "text/html", index_html, sizeof(index_html) - 1u },
    { "/__web_console/app.js", "application/javascript", app_js, sizeof(app_js) - 1u },
    { "/__web_console/style.css", "text/css", style_css, sizeof(style_css) - 1u },
};

static int path_equal_ignore_query(const char *a, const char *b) {
    size_t n = 0;
    while (a[n] && a[n] != '?') n++;
    return strlen(b) == n && memcmp(a, b, n) == 0;
}

const web_console_asset_t *web_console_asset_find(const char *path) {
    if (!path) return NULL;
    for (size_t i = 0; i < sizeof(assets) / sizeof(assets[0]); i++) {
        if (path_equal_ignore_query(path, assets[i].path)) return &assets[i];
    }
    return NULL;
}
