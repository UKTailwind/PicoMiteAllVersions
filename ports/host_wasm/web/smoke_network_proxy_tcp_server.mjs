// Headless smoke for host-WASM proxy-mode TCP server and transmit helpers.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8140;
const TCP_SERVER_PORT = 8141;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

function startProxy() {
    const proxyPath = new URL('../wasm_network_proxy', import.meta.url).pathname;
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
                if (caps?.features?.tcp_server === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with tcp_server capability');
}

async function readOutput(page) {
    return page.evaluate(async () => {
        try {
            const bytes = await window.picomite.fsRead('/sd/net_proxy_tcp_server.txt');
            return new TextDecoder().decode(bytes);
        } catch {
            return '';
        }
    });
}

async function waitForOutput(page, marker, logs, loops = 100) {
    let output = '';
    for (let i = 0; i < loops; i++) {
        output = await readOutput(page);
        if (output.includes(marker)) return output;
        await sleep(200);
    }
    fail(`timed out waiting for ${marker}; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
}

async function fetchServer(path) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 8000);
    try {
        return await fetch(`http://127.0.0.1:${TCP_SERVER_PORT}${path}`, {
            cache: 'no-store',
            signal: controller.signal,
        });
    } finally {
        clearTimeout(timer);
    }
}

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
            () => window.picomite?.proxy?.caps?.features?.tcp_server === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const mainProg = [
            '10 OPEN "net_proxy_tcp_server.txt" FOR OUTPUT AS #1',
            '20 OPTION BASE 0',
            '30 X%=456',
            '40 DIM INTEGER B%(2048/8),O%(1024/8)',
            '50 CR$=CHR$(13)+CHR$(10)',
            '60 PRINT #1,"TCPIP=";MM.INFO(TCPIP STATUS)',
            '70 PRINT #1,"TCPPORT=";MM.INFO(TCP PORT)',
            '80 PRINT #1,"MAXCONN=";MM.INFO(MAX CONNECTIONS)',
            '90 PRINT #1,"SERVERREADY"',
            '100 HANDLED%=0',
            '110 FOR T%=1 TO 500',
            '120 FOR C=1 TO MM.INFO(MAX CONNECTIONS)',
            '130 IF MM.INFO(TCP REQUEST C) THEN GOSUB 300:HANDLED%=HANDLED%+1',
            '140 NEXT C',
            '150 IF HANDLED%>=4 THEN GOTO 190',
            '160 PAUSE 20',
            '170 NEXT T%',
            '180 PRINT #1,"SERVER_TIMEOUT"',
            '190 PRINT #1,"SERVERDONE"',
            '200 CLOSE #1',
            '210 END',
            '300 WEB TCP READ C,B%()',
            '310 P$=MM.INFO(TCP PATH C)',
            '320 PRINT #1,"REQ=";P$;";LEN=";LLEN(B%())',
            '330 IF P$="/wasm/page" THEN WEB TRANSMIT PAGE C,"netpage.htm":RETURN',
            '340 IF P$="/wasm/file" THEN WEB TRANSMIT FILE C,"netfile.txt","text/plain":RETURN',
            '350 IF P$="/wasm/code" THEN WEB TRANSMIT CODE C,404:RETURN',
            '360 LONGSTRING CLEAR O%()',
            '370 LONGSTRING APPEND O%(),"HTTP/1.0 200 OK"+CR$',
            '380 LONGSTRING APPEND O%(),"Content-Type: text/plain"+CR$',
            '390 LONGSTRING APPEND O%(),"Connection: close"+CR$+CR$',
            '400 LONGSTRING APPEND O%(),"WASM_TCP_SERVER_OK"+CHR$(10)',
            '410 LONGSTRING APPEND O%(),"PATH="+P$+CHR$(10)',
            '420 LONGSTRING APPEND O%(),"LEN="+STR$(LLEN(B%()))+CHR$(10)',
            '430 WEB TCP SEND C,O%()',
            '440 WEB TCP CLOSE C',
            '450 RETURN',
        ].join('\n') + '\n';

        const afterProg = [
            '10 OPEN "net_proxy_tcp_server.txt" FOR APPEND AS #1',
            '20 DIM INTEGER B%(2048/8),O%(1024/8)',
            '30 CR$=CHR$(13)+CHR$(10)',
            '40 PRINT #1,"RUNREADY"',
            '50 FOR T%=1 TO 500',
            '60 FOR C=1 TO MM.INFO(MAX CONNECTIONS)',
            '70 IF MM.INFO(TCP REQUEST C) THEN GOSUB 200:CLOSE #1:END',
            '80 NEXT C',
            '90 PAUSE 20',
            '100 NEXT T%',
            '110 PRINT #1,"RUN_TIMEOUT"',
            '120 CLOSE #1',
            '130 END',
            '200 WEB TCP READ C,B%()',
            '210 P$=MM.INFO(TCP PATH C)',
            '220 PRINT #1,"RUNREQ=";P$;";LEN=";LLEN(B%())',
            '230 LONGSTRING CLEAR O%()',
            '240 LONGSTRING APPEND O%(),"HTTP/1.0 200 OK"+CR$+"Content-Type: text/plain"+CR$+"Connection: close"+CR$+CR$',
            '250 LONGSTRING APPEND O%(),"AFTER_RUN_OK PATH="+P$+CHR$(10)',
            '260 WEB TCP SEND C,O%()',
            '270 WEB TCP CLOSE C',
            '280 RETURN',
        ].join('\n') + '\n';

        await page.evaluate(({ mainProg, afterProg }) => {
            const enc = new TextEncoder();
            window.picomite.fsWrite('/sd/net_proxy_tcp_server.bas', enc.encode(mainProg));
            window.picomite.fsWrite('/sd/tcp_after.bas', enc.encode(afterProg));
            window.picomite.fsWrite('/sd/netpage.htm', enc.encode('PAGE_HELPER {X%}\n'));
            window.picomite.fsWrite('/sd/netfile.txt', enc.encode('FILE_HELPER\n'));
        }, { mainProg, afterProg });

        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type(`OPTION TCP SERVER PORT ${TCP_SERVER_PORT}`, { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(500);
        await page.keyboard.type('RUN "net_proxy_tcp_server.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        await sleep(1000);

        let normal;
        let normalText = '';
        let pageResp;
        let pageText = '';
        let fileResp;
        let fileText = '';
        let codeResp;
        let codeText = '';
        try {
            normal = await fetchServer('/wasm/path?x=1');
            normalText = await normal.text();
            pageResp = await fetchServer('/wasm/page');
            pageText = await pageResp.text();
            fileResp = await fetchServer('/wasm/file');
            fileText = await fileResp.text();
            codeResp = await fetchServer('/wasm/code');
            codeText = await codeResp.text();
        } catch (e) {
            const output = await readOutput(page);
            fail(`fetch to BASIC TCP server failed: ${e?.message || e}\nOutput:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        await waitForOutput(page, 'SERVERDONE', logs);
        await page.keyboard.type('RUN "tcp_after.bas"', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(1000);

        const afterResp = await fetchServer('/wasm/after-run');
        const afterText = await afterResp.text();
        const output = await waitForOutput(page, 'RUNREQ=', logs);

        const checks = [
            ['TCPIP online', /TCPIP=\s*1\b/.test(output)],
            ['TCP port set', new RegExp(`TCPPORT=\\s*${TCP_SERVER_PORT}\\b`).test(output)],
            ['max connections', /MAXCONN=\s*4\b/.test(output)],
            ['manual response body', normalText.includes('WASM_TCP_SERVER_OK')],
            ['manual response path', normalText.includes('PATH=/wasm/path?x=1')],
            ['manual read length', /LEN=\s*[1-9][0-9]*/.test(normalText)],
            ['page helper', pageText.includes('PAGE_HELPER 456')],
            ['file helper', fileText.includes('FILE_HELPER')],
            ['code helper status', codeResp.status === 404],
            ['code helper body', codeText.includes('404')],
            ['BASIC saw path', /REQ=\/wasm\/path\?x=1;LEN=\s*[1-9][0-9]*/.test(output)],
            ['after RUN response', afterText.includes('AFTER_RUN_OK PATH=/wasm/after-run')],
            ['after RUN BASIC path', /RUNREQ=\/wasm\/after-run;LEN=\s*[1-9][0-9]*/.test(output)],
        ];
        for (const [label, ok] of checks) {
            if (!ok) {
                fail(`${label} missing.\nOutput:\n${output}\nNormal:\n${normalText}\nPage:\n${pageText}\nFile:\n${fileText}\nCode ${codeResp.status}:\n${codeText}\nAfter:\n${afterText}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
            }
        }

        console.log('OK - proxy mode detected tcp_server capability.');
        console.log('OK - BASIC TCP server handled path/read/send/close through proxy slots.');
        console.log('OK - WEB TRANSMIT CODE, PAGE, and FILE helpers responded through the proxy listener.');
        console.log('OK - TCP listener survived RUN and served a new request.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
}
