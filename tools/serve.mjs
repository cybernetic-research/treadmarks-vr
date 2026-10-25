// treadmarks-vr: tiny zero-dependency static server for the game.
//
//   node tools/serve.mjs                      -> http://localhost:8095  (WebXR works on localhost)
//   node tools/serve.mjs --port 9000
//   node tools/serve.mjs --data <dir>         game data folder (default ../treadmarks-vr-upstream/Resources)
//   node tools/serve.mjs --manifest <file>    write the data manifest for static hosting, then exit
//
// web/ is served at /, the game data at /data/. /data/manifest.json lists every data file (path, size, mtime) so the
// page can fetch them and keep them in its cache. Range requests are supported so music can stream.
// On a Quest, run `adb reverse tcp:8095 tcp:8095` and open http://localhost:8095 in the headset browser.

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const args = process.argv.slice(2);
const arg = (name, def) => { const i = args.indexOf(name); return i >= 0 && args[i + 1] ? args[i + 1] : def; };
const port = Number(arg('--port', 8095));
const dataDir = path.resolve(arg('--data', path.join(root, '..', 'treadmarks-vr-upstream', 'Resources')));
const webDir = path.join(root, 'web');

const types = {
	'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
	'.wasm': 'application/wasm', '.json': 'application/json', '.css': 'text/css', '.png': 'image/png',
	'.ogg': 'audio/ogg', '.wav': 'audio/wav', '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

function walk(dir, base = '') {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const rel = base ? base + '/' + e.name : e.name;
		if (e.isDirectory()) out.push(...walk(path.join(dir, e.name), rel));
		else if (e.isFile() && rel !== 'manifest.json') {
			const st = fs.statSync(path.join(dir, e.name));
			out.push({ p: rel, s: st.size, t: Math.floor(st.mtimeMs / 1000) });
		}
	}
	return out;
}
const manifest = () => JSON.stringify({ files: walk(dataDir) });

if (args.includes('--manifest')) {
	fs.writeFileSync(arg('--manifest'), manifest());
	console.log('treadmarks-vr: wrote ' + arg('--manifest'));
	process.exit(0);
}

let cachedManifest = null;
http.createServer((req, res) => {
	let url;
	try { url = decodeURIComponent(new URL(req.url, 'http://x').pathname); } catch { res.writeHead(400).end(); return; }
	if (url === '/data/manifest.json') {
		cachedManifest ??= manifest();
		res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' }).end(cachedManifest);
		return;
	}
	const [base, rel] = url.startsWith('/data/') ? [dataDir, url.slice(6)] : [webDir, url === '/' ? 'index.html' : url.slice(1)];
	const file = path.join(base, rel);
	if (!file.startsWith(base + path.sep) && file !== base) { res.writeHead(403).end(); return; }
	fs.stat(file, (err, st) => {
		if (err || !st.isFile()) { res.writeHead(404).end('not found'); return; }
		const headers = {
			'Content-Type': types[path.extname(file).toLowerCase()] || 'application/octet-stream',
			'Accept-Ranges': 'bytes',
			'Cache-Control': 'no-cache',
		};
		const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
		if (range) {
			let start = range[1] ? Number(range[1]) : st.size - Number(range[2]);
			let end = range[1] && range[2] ? Number(range[2]) : st.size - 1;
			start = Math.max(0, start); end = Math.min(end, st.size - 1);
			if (start > end) { res.writeHead(416, { 'Content-Range': 'bytes */' + st.size }).end(); return; }
			res.writeHead(206, { ...headers, 'Content-Range': `bytes ${start}-${end}/${st.size}`, 'Content-Length': end - start + 1 });
			if (req.method === 'HEAD') { res.end(); return; }
			fs.createReadStream(file, { start, end }).pipe(res);
			return;
		}
		res.writeHead(200, { ...headers, 'Content-Length': st.size });
		if (req.method === 'HEAD') { res.end(); return; }
		fs.createReadStream(file).pipe(res);
	});
}).listen(port, () => {
	console.log(`treadmarks-vr: http://localhost:${port}  (data: ${dataDir})`);
});
