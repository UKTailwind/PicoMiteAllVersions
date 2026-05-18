// Headless smoke for browser MQTT-over-WebSocket support.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { createServer as createHttpServer } from 'node:http';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const HTTP_PORT = 8129;
const MQTT_PORT = 8130;
const PAGE_URL = `http://127.0.0.1:${HTTP_PORT}/`;

function startServer() {
    const cwd = new URL('.', import.meta.url).pathname;
    const child = spawn('python3', ['./serve.py', String(HTTP_PORT)], {
        cwd, stdio: ['ignore', 'ignore', 'inherit'],
    });
    child.unref();
    return child;
}

async function waitForPort() {
    for (let i = 0; i < 40; i++) {
        try {
            const r = await fetch(PAGE_URL);
            if (r.ok) return;
        } catch {}
        await sleep(200);
    }
    throw new Error('server did not come up');
}

function encodeRemainingLength(value) {
    const out = [];
    do {
        let encoded = value % 128;
        value = Math.floor(value / 128);
        if (value) encoded |= 128;
        out.push(encoded);
    } while (value);
    return Buffer.from(out);
}

function mqttString(text) {
    const body = Buffer.from(text);
    return Buffer.concat([Buffer.from([body.length >> 8, body.length & 255]), body]);
}

function mqttPacket(header, body = Buffer.alloc(0)) {
    return Buffer.concat([Buffer.from([header]), encodeRemainingLength(body.length), body]);
}

function mqttPublish(topic, payload) {
    const body = Buffer.concat([mqttString(topic), Buffer.from(payload)]);
    return mqttPacket(0x30, body);
}

function wsFrame(payload) {
    if (payload.length < 126) {
        return Buffer.concat([Buffer.from([0x82, payload.length]), payload]);
    }
    return Buffer.concat([
        Buffer.from([0x82, 126, payload.length >> 8, payload.length & 255]),
        payload,
    ]);
}

function decodeWsFrames(state, chunk) {
    state.buf = Buffer.concat([state.buf, chunk]);
    const frames = [];
    for (;;) {
        if (state.buf.length < 2) return frames;
        const b0 = state.buf[0];
        const b1 = state.buf[1];
        const masked = !!(b1 & 0x80);
        let len = b1 & 0x7f;
        let pos = 2;
        if (len === 126) {
            if (state.buf.length < 4) return frames;
            len = state.buf.readUInt16BE(2);
            pos = 4;
        } else if (len === 127) {
            throw new Error('large websocket frame not supported in smoke');
        }
        const maskLen = masked ? 4 : 0;
        if (state.buf.length < pos + maskLen + len) return frames;
        const mask = masked ? state.buf.subarray(pos, pos + 4) : null;
        pos += maskLen;
        const payload = Buffer.from(state.buf.subarray(pos, pos + len));
        if (mask) {
            for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
        }
        state.buf = state.buf.subarray(pos + len);
        if ((b0 & 0x0f) === 8) return frames;
        frames.push(payload);
    }
}

function decodeMqtt(packet) {
    let pos = 1;
    let multiplier = 1;
    let remaining = 0;
    for (;;) {
        const encoded = packet[pos++];
        remaining += (encoded & 127) * multiplier;
        if ((encoded & 128) === 0) break;
        multiplier *= 128;
    }
    return { type: packet[0] >> 4, body: packet.subarray(pos, pos + remaining) };
}

function readMqttString(buf, pos) {
    const len = buf.readUInt16BE(pos);
    const start = pos + 2;
    return [buf.subarray(start, start + len).toString(), start + len];
}

function startMqttWsBroker() {
    const events = [];
    const server = createHttpServer();
    server.on('upgrade', (req, socket) => {
        const key = req.headers['sec-websocket-key'];
        const accept = createHash('sha1')
            .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
            .digest('base64');
        socket.write([
            'HTTP/1.1 101 Switching Protocols',
            'Upgrade: websocket',
            'Connection: Upgrade',
            `Sec-WebSocket-Accept: ${accept}`,
            'Sec-WebSocket-Protocol: mqtt',
            '\r\n',
        ].join('\r\n'));

        const state = { buf: Buffer.alloc(0) };
        socket.on('data', (chunk) => {
            for (const frame of decodeWsFrames(state, chunk)) {
                const packet = decodeMqtt(frame);
                events.push(packet.type);
                if (packet.type === 1) {
                    socket.write(wsFrame(mqttPacket(0x20, Buffer.from([0, 0]))));
                } else if (packet.type === 8) {
                    const id = packet.body.subarray(0, 2);
                    socket.write(wsFrame(mqttPacket(0x90, Buffer.from([id[0], id[1], 0]))));
                    socket.write(wsFrame(mqttPublish('wasm/in', 'WS_OK')));
                } else if (packet.type === 3) {
                    const [topic, pos] = readMqttString(packet.body, 0);
                    const payload = packet.body.subarray(pos).toString();
                    events.push(`publish:${topic}:${payload}`);
                } else if (packet.type === 10) {
                    const id = packet.body.subarray(0, 2);
                    socket.write(wsFrame(mqttPacket(0xB0, id)));
                }
            }
        });
    });
    return new Promise((resolve) => {
        server.listen(MQTT_PORT, '127.0.0.1', () => resolve({ server, events }));
    });
}

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

const server = startServer();
let broker = null;
try {
    await waitForPort();
    broker = await startMqttWsBroker();
    const browser = await chromium.launch({
        headless: true,
        args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
    });
    try {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        const logs = [];
        page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
        page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });
        await page.waitForFunction(() => !!window.picomite?.memoryU32, { timeout: 25000 });
        await page.click('#screen');

        const prog = [
            '10 OPEN "mqtt_ws.txt" FOR OUTPUT AS #1',
            `20 WEB MQTT CONNECT "127.0.0.1/mqtt",${MQTT_PORT},"",""`,
            '30 WEB MQTT SUBSCRIBE "wasm/in",0',
            '40 FOR I%=1 TO 40',
            '50 PAUSE 100',
            '60 IF MM.MESSAGE$<>"" THEN EXIT FOR',
            '70 NEXT I%',
            '80 PRINT #1,"TOPIC=";MM.TOPIC$',
            '90 PRINT #1,"MESSAGE=";MM.MESSAGE$',
            '100 WEB MQTT PUBLISH "wasm/out","FROM_BASIC",0,0',
            '110 PAUSE 200',
            '120 WEB MQTT UNSUBSCRIBE "wasm/in"',
            '130 WEB MQTT CLOSE',
            '140 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/mqtt_ws.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('RUN "mqtt_ws.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        let output = '';
        for (let i = 0; i < 70; i++) {
            output = await page.evaluate(async () => {
                try {
                    const bytes = await window.picomite.fsRead('/sd/mqtt_ws.txt');
                    return new TextDecoder().decode(bytes);
                } catch {
                    return '';
                }
            });
            if (output.includes('MESSAGE=')) break;
            await sleep(200);
        }

        if (!/TOPIC=\s*wasm\/in/.test(output) || !/MESSAGE=\s*WS_OK/.test(output)) {
            fail(`MQTT inbound message missing:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }
        await sleep(300);
        if (!broker.events.includes('publish:wasm/out:FROM_BASIC')) {
            fail(`MQTT publish not observed by broker: ${JSON.stringify(broker.events)}`);
        }

        console.log('OK - browser MQTT-over-WebSocket connect/subscribe/receive.');
        console.log('OK - browser MQTT-over-WebSocket publish reached broker.');
        console.log('All browser MQTT WebSocket smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    if (broker) broker.server.close();
    server.kill('SIGTERM');
}
