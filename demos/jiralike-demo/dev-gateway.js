#!/usr/bin/env node
/**
 * Lightweight HTTP + FastCGI Gateway & Angular SPA Server for local testing
 * without requiring a system-wide Nginx installation.
 *
 * - Serves Angular SPA static files from web/dist/web/browser
 * - Forwards /api/* requests via FastCGI protocol to 127.0.0.1:9002 (jiralike-demo)
 */

const http = require('http');
const net = require('net');
const fs = require('fs');
const path = require('path');

const HTTP_PORT = Number(process.env.PORT || 4200);
const FCGI_HOST = process.env.FCGI_HOST || '127.0.0.1';
const FCGI_PORT = Number(process.env.FCGI_PORT || 9002);
const STATIC_ROOT = path.join(__dirname, 'web', 'dist', 'web', 'browser');

const FCGI_VERSION_1 = 1;
const FCGI_BEGIN_REQUEST = 1;
const FCGI_END_REQUEST = 3;
const FCGI_PARAMS = 4;
const FCGI_STDIN = 5;
const FCGI_STDOUT = 6;
const FCGI_STDERR = 7;
const FCGI_RESPONDER = 1;

function makeRecord(type, requestId, contentBuf) {
  const contentLength = contentBuf.length;
  const paddingLength = (8 - (contentLength % 8)) % 8;
  const header = Buffer.alloc(8);
  header[0] = FCGI_VERSION_1;
  header[1] = type;
  header.writeUInt16BE(requestId, 2);
  header.writeUInt16BE(contentLength, 4);
  header[6] = paddingLength;
  header[7] = 0;
  const pad = paddingLength > 0 ? Buffer.alloc(paddingLength) : Buffer.alloc(0);
  return Buffer.concat([header, contentBuf, pad]);
}

function encodeNameValue(name, value) {
  const nameBuf = Buffer.from(String(name), 'utf8');
  const valBuf = Buffer.from(String(value ?? ''), 'utf8');

  const encodeLen = (len) => {
    if (len < 128) {
      const b = Buffer.alloc(1);
      b[0] = len;
      return b;
    }
    const b = Buffer.alloc(4);
    b[0] = ((len >> 24) & 0x7f) | 0x80;
    b[1] = (len >> 16) & 0xff;
    b[2] = (len >> 8) & 0xff;
    b[3] = len & 0xff;
    return b;
  };

  return Buffer.concat([encodeLen(nameBuf.length), encodeLen(valBuf.length), nameBuf, valBuf]);
}

function forwardFastCGI(req, res, bodyBuf) {
  const reqId = 1;
  const urlObj = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  const params = {
    GATEWAY_INTERFACE: 'CGI/1.1',
    SERVER_SOFTWARE: 'jiralike-dev-gateway',
    SERVER_PROTOCOL: `HTTP/${req.httpVersion}`,
    REQUEST_METHOD: req.method || 'GET',
    SCRIPT_NAME: urlObj.pathname,
    REQUEST_URI: req.url,
    QUERY_STRING: urlObj.search ? urlObj.search.slice(1) : '',
    CONTENT_TYPE: req.headers['content-type'] || '',
    CONTENT_LENGTH: String(bodyBuf.length),
    REMOTE_ADDR: req.socket.remoteAddress || '127.0.0.1',
    SERVER_NAME: urlObj.hostname,
    SERVER_PORT: String(HTTP_PORT),
  };

  for (const [k, v] of Object.entries(req.headers)) {
    if (v !== undefined) {
      const envName = 'HTTP_' + k.toUpperCase().replace(/-/g, '_');
      params[envName] = Array.isArray(v) ? v.join(', ') : String(v);
    }
  }

  const beginBody = Buffer.alloc(8);
  beginBody.writeUInt16BE(FCGI_RESPONDER, 0);
  beginBody[2] = 0; // close connection after request

  const paramPairs = Buffer.concat(
    Object.entries(params).map(([k, v]) => encodeNameValue(k, v))
  );

  const chunksToSend = [
    makeRecord(FCGI_BEGIN_REQUEST, reqId, beginBody),
    makeRecord(FCGI_PARAMS, reqId, paramPairs),
    makeRecord(FCGI_PARAMS, reqId, Buffer.alloc(0)),
  ];

  let offset = 0;
  while (offset < bodyBuf.length) {
    const slice = bodyBuf.subarray(offset, offset + 32768);
    chunksToSend.push(makeRecord(FCGI_STDIN, reqId, slice));
    offset += slice.length;
  }
  chunksToSend.push(makeRecord(FCGI_STDIN, reqId, Buffer.alloc(0)));

  const sock = net.createConnection({ host: FCGI_HOST, port: FCGI_PORT }, () => {
    sock.write(Buffer.concat(chunksToSend));
  });

  let rxBuf = Buffer.alloc(0);
  const stdoutChunks = [];

  sock.on('data', (chunk) => {
    rxBuf = Buffer.concat([rxBuf, chunk]);
    while (rxBuf.length >= 8) {
      const type = rxBuf[1];
      const contentLen = rxBuf.readUInt16BE(4);
      const padLen = rxBuf[6];
      const totalLen = 8 + contentLen + padLen;
      if (rxBuf.length < totalLen) break;

      const content = rxBuf.subarray(8, 8 + contentLen);
      rxBuf = rxBuf.subarray(totalLen);

      if (type === FCGI_STDOUT && contentLen > 0) {
        stdoutChunks.push(content);
      } else if (type === FCGI_END_REQUEST) {
        sock.end();
      }
    }
  });

  sock.on('close', () => {
    const fullOut = Buffer.concat(stdoutChunks);
    const sepIdx = fullOut.indexOf('\r\n\r\n');
    if (sepIdx === -1) {
      res.writeHead(502, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Invalid FastCGI response from backend' }));
      return;
    }

    const rawHeaders = fullOut.subarray(0, sepIdx).toString('utf8');
    const respBody = fullOut.subarray(sepIdx + 4);

    let statusCode = 200;
    const outHeaders = {};
    for (const line of rawHeaders.split('\r\n')) {
      if (line.startsWith('HTTP/')) {
        const m = line.match(/^HTTP\/\d\.\d\s+(\d+)/);
        if (m) statusCode = Number(m[1]);
        continue;
      }
      const colon = line.indexOf(':');
      if (colon > 0) {
        const hk = line.slice(0, colon).trim();
        const hv = line.slice(colon + 1).trim();
        if (hk.toLowerCase() === 'status') {
          const m = hv.match(/^(\d+)/);
          if (m) statusCode = Number(m[1]);
        } else {
          outHeaders[hk] = hv;
        }
      }
    }

    res.writeHead(statusCode, outHeaders);
    res.end(respBody);
  });

  sock.on('error', (err) => {
    if (!res.headersSent) {
      res.writeHead(502, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: `FastCGI backend unreachable: ${err.message}` }));
    }
  });
}

const MIME_MAP = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.ico': 'image/x-icon',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
};

const server = http.createServer((req, res) => {
  const urlObj = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  if (urlObj.pathname.startsWith('/api/')) {
    const bodyChunks = [];
    req.on('data', (c) => bodyChunks.push(c));
    req.on('end', () => {
      forwardFastCGI(req, res, Buffer.concat(bodyChunks));
    });
    return;
  }

  let filePath = path.join(STATIC_ROOT, path.normalize(urlObj.pathname));
  if (!filePath.startsWith(STATIC_ROOT)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
    const ext = path.extname(filePath);
    res.writeHead(200, { 'Content-Type': MIME_MAP[ext] || 'application/octet-stream' });
    fs.createReadStream(filePath).pipe(res);
  } else {
    const indexHtml = path.join(STATIC_ROOT, 'index.html');
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    fs.createReadStream(indexHtml).pipe(res);
  }
});

server.listen(HTTP_PORT, '0.0.0.0', () => {
  console.log(`[jiralike-gateway] Serving Angular SPA + FastCGI (/api -> ${FCGI_HOST}:${FCGI_PORT}) at http://localhost:${HTTP_PORT}`);
});
