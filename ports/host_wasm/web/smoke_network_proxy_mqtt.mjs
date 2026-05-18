// Headless smoke for host-WASM proxy-mode plain MQTT over TCP.
//
// Starts an in-process TCP MQTT peer, serves the app through
// ports/host_native/build/wasm_network_proxy, then verifies WEB MQTT CONNECT/SUBSCRIBE,
// inbound PUBLISH state, outbound QoS 1 PUBLISH/PUBACK, UNSUBSCRIBE, and
// CLOSE all flow through the C proxy TCP stream backend.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import net from 'node:net';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8144;
const MQTT_PORT = 8145;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

function mqttString(text) {
    const bytes = Buffer.from(text);
    return Buffer.concat([Buffer.from([bytes.length >> 8, bytes.length & 0xff]), bytes]);
}

function mqttPacket(header, body = Buffer.alloc(0)) {
    const rem = [];
    let n = body.length;
    do {
        let b = n % 128;
        n = Math.floor(n / 128);
        if (n) b |= 128;
        rem.push(b);
    } while (n);
    return Buffer.concat([Buffer.from([header, ...rem]), body]);
}

function mqttPublish(topic, payload) {
    return mqttPacket(0x30, Buffer.concat([mqttString(topic), Buffer.from(payload)]));
}

function readMqttString(body, pos = 0) {
    const len = body.readUInt16BE(pos);
    const start = pos + 2;
    return { value: body.subarray(start, start + len).toString(), next: start + len };
}

function parsePublish(header, body) {
    const t = readMqttString(body, 0);
    let pos = t.next;
    let packetId = 0;
    if (((header >> 1) & 3) > 0) {
        packetId = body.readUInt16BE(pos);
        pos += 2;
    }
    return {
        topic: t.value,
        payload: body.subarray(pos).toString(),
        packetId,
    };
}

function drainPackets(state, chunk, onPacket) {
    state.buf = Buffer.concat([state.buf, chunk]);
    for (;;) {
        if (state.buf.length < 2) return;
        let multiplier = 1;
        let remaining = 0;
        let pos = 1;
        for (;;) {
            if (pos >= state.buf.length) return;
            const encoded = state.buf[pos++];
            remaining += (encoded & 127) * multiplier;
            if ((encoded & 128) === 0) break;
            multiplier *= 128;
            if (multiplier > 128 * 128 * 128) throw new Error('bad remaining length');
        }
        if (state.buf.length < pos + remaining) return;
        const header = state.buf[0];
        const body = state.buf.subarray(pos, pos + remaining);
        state.buf = state.buf.subarray(pos + remaining);
        onPacket(header, body);
    }
}

async function startMqttPeer() {
    const events = [];
    const server = net.createServer((socket) => {
        const state = { buf: Buffer.alloc(0), publishedInbound: false };
        socket.on('data', (chunk) => drainPackets(state, chunk, (header, body) => {
            const type = header >> 4;
            if (type === 1) {
                const proto = readMqttString(body, 0);
                events.push(`connect:${proto.value}`);
                socket.write(mqttPacket(0x20, Buffer.from([0, 0])));
            } else if (type === 8) {
                const id = body.readUInt16BE(0);
                const topic = readMqttString(body, 2).value;
                events.push(`subscribe:${topic}`);
                socket.write(mqttPacket(0x90, Buffer.from([id >> 8, id & 0xff, 0])));
                if (!state.publishedInbound) {
                    state.publishedInbound = true;
                    setTimeout(() => {
                        if (!socket.destroyed) socket.write(mqttPublish('proxy/in', 'PLAIN_OK'));
                    }, 80);
                }
            } else if (type === 3) {
                const pub = parsePublish(header, body);
                events.push(`publish:${pub.topic}:${pub.payload}:qos${(header >> 1) & 3}`);
                if (pub.packetId) {
                    socket.write(mqttPacket(0x40, Buffer.from([pub.packetId >> 8, pub.packetId & 0xff])));
                }
            } else if (type === 10) {
                const id = body.readUInt16BE(0);
                const topic = readMqttString(body, 2).value;
                events.push(`unsubscribe:${topic}`);
                socket.write(mqttPacket(0xB0, Buffer.from([id >> 8, id & 0xff])));
            } else if (type === 14) {
                events.push('disconnect');
                socket.end();
            } else {
                events.push(`type:${type}`);
            }
        }));
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(MQTT_PORT, '127.0.0.1', resolve);
    });
    return { server, events };
}

function startProxy() {
    const proxyPath = new URL('../../host_native/build/wasm_network_proxy', import.meta.url).pathname;
    const webRoot = new URL('.', import.meta.url).pathname;
    const child = spawn(proxyPath, ['--port', String(PROXY_PORT), '--web-root', webRoot], {
        cwd: new URL('../..', import.meta.url).pathname,
        stdio: ['ignore', 'pipe', 'inherit'],
    });
    child.stdout.on('data', (buf) => process.stdout.write(buf));
    child.unref();
    return child;
}

async function waitForProxy() {
    const url = `${PAGE_URL}__picomite_proxy/caps`;
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(url);
            if (r.ok) {
                const caps = await r.json();
                if (caps?.features?.tcp_stream === true &&
                    caps?.features?.mqtt_plain === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with mqtt_plain capability');
}

const broker = await startMqttPeer();
const proxy = startProxy();

try {
    await waitForProxy();
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
        await page.waitForFunction(() => window.picomite?.proxy?.online === true, { timeout: 5000 });
        await page.waitForFunction(
            () => window.picomite?.proxy?.caps?.features?.mqtt_plain === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const prog = [
            '10 OPEN "mqtt_proxy.txt" FOR OUTPUT AS #1',
            `20 WEB MQTT CONNECT "127.0.0.1",${MQTT_PORT},"",""`,
            '30 WEB MQTT SUBSCRIBE "proxy/in",0',
            '40 FOR I%=1 TO 50',
            '50 PAUSE 100',
            '60 IF MM.MESSAGE$<>"" THEN EXIT FOR',
            '70 NEXT I%',
            '80 PRINT #1,"TOPIC=";MM.TOPIC$',
            '90 PRINT #1,"MESSAGE=";MM.MESSAGE$',
            '100 WEB MQTT PUBLISH "proxy/out","FROM_PROXY_BASIC",1,0',
            '110 WEB MQTT UNSUBSCRIBE "proxy/in"',
            '120 WEB MQTT CLOSE',
            '130 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/mqtt_proxy.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('RUN "mqtt_proxy.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        let output = '';
        for (let i = 0; i < 80; i++) {
            output = await page.evaluate(async () => {
                try {
                    const bytes = await window.picomite.fsRead('/sd/mqtt_proxy.txt');
                    return new TextDecoder().decode(bytes);
                } catch {
                    return '';
                }
            });
            if (output.includes('MESSAGE=')) break;
            await sleep(200);
        }
        if (!/TOPIC=\s*proxy\/in/.test(output) || !/MESSAGE=\s*PLAIN_OK/.test(output)) {
            fail(`MQTT inbound message missing:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        for (let i = 0; i < 20; i++) {
            if (broker.events.includes('disconnect')) break;
            await sleep(100);
        }
        const required = [
            'connect:MQTT',
            'subscribe:proxy/in',
            'publish:proxy/out:FROM_PROXY_BASIC:qos1',
            'unsubscribe:proxy/in',
            'disconnect',
        ];
        for (const event of required) {
            if (!broker.events.includes(event)) {
                fail(`broker event ${event} missing: ${JSON.stringify(broker.events)}\nOutput:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
            }
        }

        console.log('OK - proxy mode detected mqtt_plain capability.');
        console.log('OK - plain MQTT CONNECT/SUBSCRIBE/PUBLISH/UNSUBSCRIBE/CLOSE used proxy TCP stream.');
        console.log('All proxy plain MQTT smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
    await new Promise((resolve) => broker.server.close(resolve));
}
