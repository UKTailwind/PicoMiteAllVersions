// Headless smoke for the browser build's network command surface.
//
// The browser build intentionally has no raw TCP/UDP/Telnet socket access, but
// can service simple HTTP client requests through browser fetch. Verify that
// the shipping app still boots, status MM.INFO selectors report offline values,
// raw-only commands fail explicitly, and WEB TCP CLIENT REQUEST returns an HTTP
// response through the BASIC long-string buffer.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8128;
const PAGE_URL = `http://127.0.0.1:${PORT}/`;

function startServer() {
    const cwd = new URL('.', import.meta.url).pathname;
    const child = spawn('python3', ['./serve.py', String(PORT)], {
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

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

const server = startServer();
try {
    await waitForPort();
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
            '10 OPEN "net_unsupported.txt" FOR OUTPUT AS #1',
            '20 DIM INTEGER A%(4096/8)',
            '25 DIM INTEGER B%(4096/8)',
            '30 CR$=CHR$(13)+CHR$(10)',
            '40 PRINT #1,"TCPIP=";MM.INFO(TCPIP STATUS)',
            '50 PRINT #1,"WIFI=";MM.INFO(WIFI STATUS)',
            '60 PRINT #1,"IP=[";MM.INFO(IP ADDRESS);"]"',
            '70 PRINT #1,"TCPPORT=";MM.INFO(TCP PORT)',
            '80 PRINT #1,"UDPPORT=";MM.INFO(UDP PORT)',
            '90 PRINT #1,"MAXCONN=";MM.INFO(MAX CONNECTIONS)',
            `100 WEB OPEN TCP CLIENT "127.0.0.1",${PORT},5000`,
            '110 WEB TCP CLIENT REQUEST "GET /picomite.css HTTP/1.0"+CR$+"Host: 127.0.0.1"+CR$+CR$,A%(),5000',
            '120 PRINT #1,"FETCHLEN=";LLEN(A%())',
            '130 PRINT #1,"FETCHHEAD=";LGETSTR$(A%(),1,12)',
            '140 PRINT #1,"FETCHCSS=";INSTR(LGETSTR$(A%(),180,240),":root")',
            '150 WEB TCP CLIENT REQUEST "POST /__smoke_echo HTTP/1.0"+CR$+"Host: 127.0.0.1"+CR$+"Content-Type: text/plain"+CR$+CR$+"FETCH_BODY",B%(),5000',
            '160 PRINT #1,"POSTLEN=";LLEN(B%())',
            '170 PRINT #1,"POSTBODY=";INSTR(LGETSTR$(B%(),240,77),"BODY=FETCH_BODY")',
            '180 PRINT #1,"POSTTYPE=";INSTR(LGETSTR$(B%(),180,136),"CONTENT_TYPE=text/plain")',
            '190 WEB CLOSE TCP CLIENT',
            `200 WEB OPEN TCP CLIENT "127.0.0.1",${PORT},5000`,
            '210 ON ERROR SKIP',
            '220 WEB TCP CLIENT REQUEST "EHLO"+CR$,A%(),5000',
            '230 PRINT #1,"RAWREQERR=";MM.ERRNO;":";MM.ERRMSG$',
            '240 WEB CLOSE TCP CLIENT',
            '250 ON ERROR SKIP',
            `260 WEB OPEN TCP STREAM "127.0.0.1",${PORT},5000`,
            '270 PRINT #1,"STREAMERR=";MM.ERRNO;":";MM.ERRMSG$',
            '280 ON ERROR SKIP',
            '290 WEB',
            '300 PRINT #1,"WEBERR=";MM.ERRNO;":";MM.ERRMSG$',
            '310 ON ERROR SKIP',
            '320 OPTION TELNET CONSOLE ON',
            '330 PRINT #1,"TELNETERR=";MM.ERRNO;":";MM.ERRMSG$',
            '340 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/net_unsupported.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('RUN "net_unsupported.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        let output = '';
        for (let i = 0; i < 50; i++) {
            output = await page.evaluate(async () => {
                try {
                    const bytes = await window.picomite.fsRead('/sd/net_unsupported.txt');
                    return new TextDecoder().decode(bytes);
                } catch {
                    return '';
                }
            });
            if (output.includes('TELNETERR=')) break;
            await sleep(200);
        }
        if (!output.includes('TELNETERR=')) {
            fail(`timed out waiting for BASIC output file; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        const checks = [
            ['TCPIP=-2', /TCPIP=\s*-2\b/],
            ['WIFI=-2', /WIFI=\s*-2\b/],
            ['IP empty', /IP=\[\]/],
            ['TCPPORT=0', /TCPPORT=\s*0\b/],
            ['UDPPORT=0', /UDPPORT=\s*0\b/],
            ['MAXCONN=0', /MAXCONN=\s*0\b/],
            ['FETCHLEN positive', /FETCHLEN=\s*[1-9][0-9]*\b/],
            ['FETCHHEAD status', /FETCHHEAD=HTTP\/1\.1 200/],
            ['FETCHCSS marker', /FETCHCSS=\s*[1-9][0-9]*\b/],
            ['POSTLEN positive', /POSTLEN=\s*[1-9][0-9]*\b/],
            ['POSTBODY marker', /POSTBODY=\s*[1-9][0-9]*\b/],
            ['POSTTYPE marker', /POSTTYPE=\s*[1-9][0-9]*\b/],
            ['RAWREQ unsupported error', /RAWREQERR=\s*[1-9][0-9]*:Only HTTP requests are supported in browser build/],
            ['STREAM unsupported error', /STREAMERR=\s*[1-9][0-9]*:WEB networking not supported in browser build/],
            ['WEB unsupported error', /WEBERR=\s*[1-9][0-9]*:WEB networking not supported in browser build/],
            ['TELNET unsupported error', /TELNETERR=\s*[1-9][0-9]*:WEB networking not supported in browser build/],
        ];
        for (const [label, re] of checks) {
            if (!re.test(output)) {
                fail(`${label} missing from output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
            }
        }

        console.log('OK - browser network status reports unsupported/offline.');
        console.log('OK - WEB TCP CLIENT REQUEST fetched same-origin GET/POST HTTP.');
        console.log('OK - raw TCP request, stream, WEB, and Telnet fail explicitly.');
        console.log('All browser network smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
