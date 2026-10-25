// treadmarks-vr: tiny zero-dependency static server for the game.
//
//   node tools/serve.mjs                      -> http://localhost:8097  (WebXR works on localhost)
//   node tools/serve.mjs --port 9000
//   node tools/serve.mjs --data <dir>         game data folder (default ../treadmarks-vr-upstream/Resources)
//   node tools/serve.mjs --manifest <file>    write the data manifest for static hosting, then exit
//   node tools/serve.mjs --https              HTTPS with a self-signed certificate (made with openssl on first use,
//                                             kept in tools/.cert), so a headset on the network can use WebXR:
//                                             open https://<this computer's address>:8097 and accept the warning.
//
// web/ is served at /, the game data at /data/. /data/manifest.json lists every data file (path, size, mtime) so the
// page can fetch them and keep them in its cache. Range requests are supported so music can stream, and compressible
// files are sent gzipped.
// On a Quest: either use --https as above, or connect it by USB, run `adb reverse tcp:8097 tcp:8097` and open
// http://localhost:8097 in the headset browser.

import http from 'node:http';
import https from 'node:https';
import os from 'node:os';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const args = process.argv.slice(2);
const arg = (name, def) => { const i = args.indexOf(name); return i >= 0 && args[i + 1] ? args[i + 1] : def; };
const port = Number(arg('--port', 8097));
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

// A self-signed certificate covering localhost and this computer's network addresses.
function certificate() {
	const dir = path.join(root, 'tools', '.cert');
	const key = path.join(dir, 'key.pem'), cert = path.join(dir, 'cert.pem');
	const ips = Object.values(os.networkInterfaces()).flat().filter((a) => a && a.family === 'IPv4' && !a.internal).map((a) => a.address);
	const san = ['DNS:localhost', 'IP:127.0.0.1', ...ips.map((ip) => 'IP:' + ip)].join(',');
	const sanFile = path.join(dir, 'san.txt');
	if (!fs.existsSync(cert) || !fs.existsSync(sanFile) || fs.readFileSync(sanFile, 'utf8') !== san) {
		fs.mkdirSync(dir, { recursive: true });
		execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '825', '-subj', '/CN=treadmarks-vr',
			'-addext', 'subjectAltName=' + san, '-keyout', key, '-out', cert], { stdio: 'ignore' });
		fs.writeFileSync(sanFile, san);
	}
	return { key: fs.readFileSync(key), cert: fs.readFileSync(cert), ips };
}

// The game's art (BMP), maps and models shrink to about 40% with gzip (251 MB -> 99 MB in all). Compressed copies are
// kept in the system temp folder, keyed by path, size and time, so each file is compressed once.
const compressible = new Set(['.bmp', '.ved', '.lwo', '.wav', '.img', '.luv', '.ent', '.txt', '.cfg', '.wasm', '.js', '.html', '.json']);
const gzDir = path.join(os.tmpdir(), 'treadmarks-vr-gz');
fs.mkdirSync(gzDir, { recursive: true });
const gzBusy = new Map();
function gzipped(file, st) {
	const key = crypto.createHash('sha1').update(`${file}|${st.size}|${st.mtimeMs}`).digest('hex');
	const out = path.join(gzDir, key + '.gz');
	if (gzBusy.has(key)) return gzBusy.get(key);
	const job = fs.promises.readFile(out).catch(async () => {
		const gz = await new Promise((resolve, reject) =>
			fs.readFile(file, (err, data) => (err ? reject(err) : zlib.gzip(data, { level: 6 }, (e, r) => (e ? reject(e) : resolve(r))))));
		await fs.promises.writeFile(out, gz).catch(() => {});
		return gz;
	}).finally(() => gzBusy.delete(key));
	gzBusy.set(key, job);
	return job;
}

let cachedManifest = null;
const useHttps = args.includes('--https');
const tls = useHttps ? certificate() : null;
(useHttps ? (h) => https.createServer({ key: tls.key, cert: tls.cert }, h) : http.createServer)((req, res) => {
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
		if (compressible.has(path.extname(file).toLowerCase()) && /\bgzip\b/.test(req.headers['accept-encoding'] || '')) {
			gzipped(file, st).then((gz) => {
				res.writeHead(200, { ...headers, 'Content-Encoding': 'gzip', 'Content-Length': gz.length, Vary: 'Accept-Encoding' });
				res.end(req.method === 'HEAD' ? undefined : gz);
			}, () => { res.writeHead(500).end(); });
			return;
		}
		res.writeHead(200, { ...headers, 'Content-Length': st.size });
		if (req.method === 'HEAD') { res.end(); return; }
		fs.createReadStream(file).pipe(res);
	});
}).listen(port, () => {
	const scheme = useHttps ? 'https' : 'http';
	console.log(`treadmarks-vr: ${scheme}://localhost:${port}  (data: ${dataDir})`);
	for (const ip of tls?.ips || []) console.log(`treadmarks-vr: ${scheme}://${ip}:${port}  (headset)`);
});
