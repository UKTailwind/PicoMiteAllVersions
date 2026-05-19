const { app, BrowserWindow, shell } = require('electron');
const fs = require('fs');
const http = require('http');
const path = require('path');

const APP_PORT = Number(process.env.MMBA_DESKTOP_PORT || 43185);

const MIME_TYPES = {
  '.bas': 'text/plain; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.data': 'application/octet-stream',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.wasm': 'application/wasm',
  '.webmanifest': 'application/manifest+json; charset=utf-8'
};

function webRoot() {
  if (app.isPackaged) return path.join(process.resourcesPath, 'web');
  return path.resolve(__dirname, '..', 'web');
}

function sendHeaders(response, status, headers = {}) {
  response.writeHead(status, {
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cross-Origin-Resource-Policy': 'same-origin',
    'Cache-Control': 'no-store',
    ...headers
  });
}

function safeFilePath(root, requestUrl) {
  const url = new URL(requestUrl, `http://127.0.0.1:${APP_PORT}/`);
  const pathname = decodeURIComponent(url.pathname);
  const relative = pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, '');
  const filePath = path.normalize(path.join(root, relative));
  if (filePath !== root && !filePath.startsWith(root + path.sep)) return null;
  return filePath;
}

function startAppServer(root, port) {
  const server = http.createServer((request, response) => {
    if (request.method === 'POST' && request.url === '/__smoke_echo') {
      const chunks = [];
      request.on('data', chunk => chunks.push(chunk));
      request.on('end', () => {
        const body = Buffer.concat(chunks);
        const payload = Buffer.concat([
          Buffer.from(`METHOD=POST\nCONTENT_TYPE=${request.headers['content-type'] || ''}\nBODY=`),
          body
        ]);
        sendHeaders(response, 200, {
          'Content-Type': 'text/plain; charset=utf-8',
          'Content-Length': String(payload.length)
        });
        response.end(payload);
      });
      return;
    }

    if (request.method !== 'GET' && request.method !== 'HEAD') {
      sendHeaders(response, 405, { Allow: 'GET, HEAD' });
      response.end('Method Not Allowed');
      return;
    }

    const filePath = safeFilePath(root, request.url || '/');
    if (!filePath) {
      sendHeaders(response, 403, { 'Content-Type': 'text/plain; charset=utf-8' });
      response.end('Forbidden');
      return;
    }

    fs.stat(filePath, (statError, stat) => {
      if (statError || !stat.isFile()) {
        sendHeaders(response, 404, { 'Content-Type': 'text/plain; charset=utf-8' });
        response.end('Not Found');
        return;
      }

      const type = MIME_TYPES[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
      sendHeaders(response, 200, {
        'Content-Type': type,
        'Content-Length': String(stat.size)
      });
      if (request.method === 'HEAD') {
        response.end();
        return;
      }
      fs.createReadStream(filePath).pipe(response);
    });
  });

  return new Promise((resolve, reject) => {
    server.on('error', error => {
      if (error.code === 'EADDRINUSE') {
        reject(new Error(`MMBasic Anywhere desktop port ${port} is already in use.`));
        return;
      }
      reject(error);
    });
    server.listen(port, '127.0.0.1', () => {
      const address = server.address();
      if (!address || typeof address === 'string') {
        reject(new Error('failed to bind app server'));
        return;
      }
      resolve({ server, url: `http://127.0.0.1:${address.port}/` });
    });
  });
}

let appServer = null;
let appUrl = null;
let mainWindow = null;

async function ensureAppServer(root) {
  if (appUrl) return appUrl;

  const { server, url } = await startAppServer(root, APP_PORT);
  appServer = server;
  appUrl = url;
  return appUrl;
}

async function createWindow() {
  const root = webRoot();
  if (!fs.existsSync(path.join(root, 'picomite.wasm'))) {
    throw new Error(`WASM bundle is missing. Run "npm run build:wasm" in ${__dirname}.`);
  }

  const url = await ensureAppServer(root);

  mainWindow = new BrowserWindow({
    width: 1280,
    height: 900,
    minWidth: 900,
    minHeight: 640,
    title: 'MMBasic Anywhere',
    backgroundColor: '#111418',
    webPreferences: {
      backgroundThrottling: false,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });

  mainWindow.webContents.setWindowOpenHandler(({ url: targetUrl }) => {
    shell.openExternal(targetUrl);
    return { action: 'deny' };
  });
  mainWindow.webContents.on('will-navigate', (event, targetUrl) => {
    if (!targetUrl.startsWith(url)) {
      event.preventDefault();
      shell.openExternal(targetUrl);
    }
  });

  await mainWindow.loadURL(url);
}

const gotSingleInstanceLock = app.requestSingleInstanceLock();

if (!gotSingleInstanceLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (!mainWindow) return;
    if (mainWindow.isMinimized()) mainWindow.restore();
    mainWindow.focus();
  });

  app.on('before-quit', () => {
    if (appServer) appServer.close();
    appServer = null;
    appUrl = null;
  });

  app.whenReady().then(createWindow).catch(error => {
    console.error(error);
    app.quit();
  });

  app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit();
  });

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow().catch(error => {
        console.error(error);
        app.quit();
      });
    }
  });
}
