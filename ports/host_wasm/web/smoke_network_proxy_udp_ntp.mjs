// Headless smoke for host-WASM proxy-mode UDP and NTP.
//
// Starts local UDP peers, serves the app through ports/host_native/build/wasm_network_proxy, then
// verifies WEB UDP SEND, OPTION UDP SERVER PORT receive state, UDP listener
// preservation across RUN, and WEB NTP through a deterministic UDP responder.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8136;
const UDP_SEND_PORT = 8137;
const UDP_SERVER_PORT = 8138;
const NTP_PORT = 8139;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/`;
const NTP_UNIX_DELTA = 2_208_988_800;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

async function bindUdp(port, onMessage) {
    const sock = dgram.createSocket('udp4');
    sock.on('message', onMessage);
    await new Promise((resolve, reject) => {
        sock.once('error', reject);
        sock.bind(port, '127.0.0.1', resolve);
    });
    return sock;
}

async function startUdpSink(log) {
    return bindUdp(UDP_SEND_PORT, (msg, rinfo) => {
        log.udpSend = { msg: Buffer.from(msg), rinfo };
    });
}

async function startNtpResponder(log) {
    const unixSeconds = Math.floor(Date.UTC(2026, 0, 2, 3, 4, 5) / 1000);
    return bindUdp(NTP_PORT, (msg, rinfo) => {
        log.ntpRequest = Buffer.from(msg);
        const response = Buffer.alloc(48);
        response[0] = 0x24;
        response[1] = 1;
        response.writeUInt32BE(unixSeconds + NTP_UNIX_DELTA, 40);
        log.ntpSocket.send(response, rinfo.port, rinfo.address);
    });
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
                if (caps?.features?.udp === true && caps?.features?.ntp === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with udp and ntp capabilities');
}

async function readOutput(page) {
    return page.evaluate(async () => {
        try {
            const bytes = await window.picomite.fsRead('/sd/net_proxy_udp_ntp.txt');
            return new TextDecoder().decode(bytes);
        } catch {
            return '';
        }
    });
}

async function waitForOutput(page, marker, logs, loops = 80) {
    let output = '';
    for (let i = 0; i < loops; i++) {
        output = await readOutput(page);
        if (output.includes(marker)) return output;
        await sleep(200);
    }
    fail(`timed out waiting for ${marker}; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
}

const log = { udpSend: null, ntpRequest: null, ntpSocket: null };
const udpSink = await startUdpSink(log);
log.ntpSocket = await startNtpResponder(log);
const injector = dgram.createSocket('udp4');
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
            () => window.picomite?.proxy?.caps?.features?.udp === true &&
                  window.picomite?.proxy?.caps?.features?.ntp === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const mainProg = [
            '10 OPEN "net_proxy_udp_ntp.txt" FOR OUTPUT AS #1',
            '20 PRINT #1,"TCPIP=";MM.INFO(TCPIP STATUS)',
            '30 PRINT #1,"UDPPORT0=";MM.INFO(UDP PORT)',
            '40 PRINT #1,"UDPPORT=";MM.INFO(UDP PORT)',
            '50 PRINT #1,"UDPREADY"',
            `60 WEB UDP SEND "127.0.0.1",${UDP_SEND_PORT},"WASM_UDP_OUT"`,
            '70 FOR I%=1 TO 50',
            '80 PAUSE 100',
            '90 IF MM.MESSAGE$<>"" THEN PRINT #1,"UDPMSG=";MM.MESSAGE$:PRINT #1,"UDPADDR=[";MM.ADDRESS$;"]":GOTO 120',
            '100 NEXT I%',
            '110 PRINT #1,"UDPMSG="',
            `120 WEB NTP 0,"127.0.0.1:${NTP_PORT}",3000`,
            '130 PRINT #1,"NTPDATE=";DATE$',
            '140 PRINT #1,"NTPTIME=";TIME$',
            '150 CLOSE #1',
        ].join('\n') + '\n';

        const afterProg = [
            '10 OPEN "net_proxy_udp_ntp.txt" FOR APPEND AS #1',
            '20 PRINT #1,"RUNREADY"',
            '30 FOR I%=1 TO 50',
            '40 PAUSE 100',
            '50 IF MM.MESSAGE$="WASM_UDP_AFTER_RUN" THEN PRINT #1,"RUNMSG=";MM.MESSAGE$:PRINT #1,"RUNADDR=[";MM.ADDRESS$;"]":GOTO 80',
            '60 NEXT I%',
            '70 PRINT #1,"RUNMSG="',
            '80 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate(({ mainProg, afterProg }) => {
            const enc = new TextEncoder();
            window.picomite.fsWrite('/sd/net_proxy_udp_ntp.bas', enc.encode(mainProg));
            window.picomite.fsWrite('/sd/udp_after.bas', enc.encode(afterProg));
        }, { mainProg, afterProg });
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type(`OPTION UDP SERVER PORT ${UDP_SERVER_PORT}`, { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(500);
        await page.keyboard.type('RUN "net_proxy_udp_ntp.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        const udpInTimer = setInterval(() => {
            injector.send(Buffer.from('WASM_UDP_IN'), UDP_SERVER_PORT, '127.0.0.1');
        }, 100);
        setTimeout(() => clearInterval(udpInTimer), 6000);

        await waitForOutput(page, 'NTPDATE=', logs, 100);
        await page.keyboard.type('RUN "udp_after.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        const afterTimer = setInterval(() => {
            injector.send(Buffer.from('WASM_UDP_AFTER_RUN'), UDP_SERVER_PORT, '127.0.0.1');
        }, 100);
        setTimeout(() => clearInterval(afterTimer), 7000);

        const output = await waitForOutput(page, 'RUNADDR=', logs, 100);
        clearInterval(udpInTimer);
        clearInterval(afterTimer);
        for (let i = 0; i < 30 && !log.udpSend; i++) await sleep(100);

        if (!log.udpSend || log.udpSend.msg.toString('utf8') !== 'WASM_UDP_OUT') {
            fail(`UDP sink did not receive expected WEB UDP SEND payload: ${log.udpSend?.msg?.toString('utf8')}`);
        }
        if (!log.ntpRequest || log.ntpRequest.length !== 48 || log.ntpRequest[0] !== 0x1b) {
            fail(`NTP responder did not receive a valid request: ${log.ntpRequest?.toString('hex')}`);
        }

        const checks = [
            ['TCPIP online', /TCPIP=\s*1\b/],
            ['UDP option set', new RegExp(`UDPPORT=\\s*${UDP_SERVER_PORT}\\b`)],
            ['UDP receive message', /UDPMSG=WASM_UDP_IN/],
            ['UDP receive address', /UDPADDR=\[[^\]]+\]/],
            ['NTP date', /NTPDATE=02-01-2026/],
            ['NTP time', /(?:NTPTIME=03:04:0[5-9]|NTPTIME=03:04:[1-3][0-9])/],
            ['RUN UDP message', /RUNMSG=WASM_UDP_AFTER_RUN/],
            ['RUN UDP address', /RUNADDR=\[[^\]]+\]/],
        ];
        for (const [label, re] of checks) {
            if (!re.test(output)) {
                fail(`${label} missing from output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
            }
        }

        console.log('OK - proxy mode detected udp and ntp capabilities.');
        console.log('OK - WEB UDP SEND and proxy-bound UDP receive updated BASIC message/address state.');
        console.log('OK - UDP listener survived RUN and WEB NTP used deterministic UDP responder.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
    injector.close();
    udpSink.close();
    log.ntpSocket.close();
}
