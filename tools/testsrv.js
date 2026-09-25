/*
    tools/testsrv.js - a tiny, slow HTTP server used to verify the UI against a
    live aria2 engine: downloads trickle in over ~15 seconds so the progress,
    speed and ETA paths in the task list are actually exercised.

        node tools/testsrv.js [port] [sizeMiB]

    Then queue a download, either through the browser bridge:

        curl "http://127.0.0.1:8899/download?url=http://127.0.0.1:8898/file1.bin"

    or straight through aria2's own JSON-RPC:

        curl http://127.0.0.1:6800/jsonrpc -d '{"jsonrpc":"2.0","id":"1",
             "method":"aria2.addUri","params":[["http://127.0.0.1:8898/file1.bin"]]}'
*/
const http = require('http');

const port = Number(process.argv[2] || 8898);
const size = Number(process.argv[3] || 12) * 1024 * 1024;

const server = http.createServer((req, res) => {
  const name = decodeURIComponent(req.url.split('?')[0].replace(/^\//, '')) || 'file.bin';
  const total = size;

  if (req.method === 'HEAD') {
    res.writeHead(200, {
      'Content-Length': String(total),
      'Content-Type': 'application/octet-stream',
      'Accept-Ranges': 'bytes',
    });
    res.end();
    return;
  }

  const range = req.headers.range;
  if (range) {
    const m = /bytes=(\d+)-(\d*)/.exec(range);
    if (m) {
      const start = Number(m[1]);
      const end = m[2] ? Number(m[2]) : total - 1;
      res.writeHead(206, {
        'Content-Length': String(end - start + 1),
        'Content-Range': `bytes ${start}-${end}/${total}`,
        'Content-Type': 'application/octet-stream',
        'Accept-Ranges': 'bytes',
      });
      trickle(res, end - start + 1);
      return;
    }
  }

  res.writeHead(200, {
    'Content-Length': String(total),
    'Content-Type': 'application/octet-stream',
    'Accept-Ranges': 'bytes',
    'Content-Disposition': `attachment; filename="${name}"`,
  });
  trickle(res, total);
});

/// ~700 KiB/s, so a 12 MiB file takes about 17 seconds.
function trickle(res, length) {
  const chunk = Buffer.alloc(64 * 1024, 0x41);
  let sent = 0;

  const pump = () => {
    if (res.writableEnded || res.destroyed) return;
    if (sent >= length) {
      res.end();
      return;
    }
    const n = Math.min(chunk.length, length - sent);
    sent += n;
    if (res.write(n === chunk.length ? chunk : chunk.subarray(0, n))) {
      setTimeout(pump, 90);
    } else {
      res.once('drain', () => setTimeout(pump, 90));
    }
  };
  pump();
}

server.listen(port, '127.0.0.1', () => {
  console.log(`test server on http://127.0.0.1:${port} (${size / 1048576} MiB per file)`);
});
