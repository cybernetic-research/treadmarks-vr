// treadmarks-vr: the page side of the port. Loads the game data into the in-memory filesystem, feeds keyboard, mouse
// and gamepad input to the game as SFML events (shim/sfml_web.cpp), plays sound with Web Audio, keeps saved settings in
// IndexedDB, and runs the game's frame once per display frame (or per headset frame, see xr.js).

import { setupXR } from './xr.js';

const $ = (id) => document.getElementById(id);
const canvas = $('canvas');
const status = (text) => { $('status').textContent = text; };
const isHeadset = /OculusBrowser|Quest|Pico|Wolvic/i.test(navigator.userAgent);

// --- keyboard: KeyboardEvent.code in the order of sf::Keyboard::Key (shim/SFML/Window.hpp) ---------------------------
const SF_KEYS = [
	...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').map((c) => 'Key' + c),
	...'0123456789'.split('').map((c) => 'Digit' + c),
	'Escape', 'ControlLeft', 'ShiftLeft', 'AltLeft', 'MetaLeft', 'ControlRight', 'ShiftRight', 'AltRight', 'MetaRight',
	'ContextMenu', 'BracketLeft', 'BracketRight', 'Semicolon', 'Comma', 'Period', 'Quote', 'Slash', 'Backslash',
	'Backquote', 'Equal', 'Minus', 'Space', 'Enter', 'Backspace', 'Tab', 'PageUp', 'PageDown', 'End', 'Home', 'Insert',
	'Delete', 'NumpadAdd', 'NumpadSubtract', 'NumpadMultiply', 'NumpadDivide', 'ArrowLeft', 'ArrowRight', 'ArrowUp',
	'ArrowDown', ...'0123456789'.split('').map((c) => 'Numpad' + c),
	...Array.from({ length: 15 }, (_, i) => 'F' + (i + 1)), 'Pause',
];
const KEY = Object.fromEntries(SF_KEYS.map((code, i) => [code, i]));
KEY.NumpadEnter = KEY.Enter;
const BROWSER_KEYS = new Set(['F5', 'F11', 'F12']);	// left to the browser (reload, fullscreen, dev tools)

// --- audio -------------------------------------------------------------------------------------------------------
class GameAudio {
	constructor() {
		this.ctx = new AudioContext();
		this.master = this.ctx.createGain();
		this.master.connect(this.ctx.destination);
		this.buffers = new Map();
		this.voices = new Map();
		this.nextId = 1;
		this.music = new Audio();
		this.music.preload = 'auto';
		this.musicVol = 1;
		this.musicPaused = false;
	}
	resume() { if (this.ctx.state !== 'running') this.ctx.resume(); }
	create(bytes) {
		const id = this.nextId++;
		this.ctx.decodeAudioData(bytes.buffer).then((b) => this.buffers.set(id, b), () => {});
		return id;
	}
	free(id) { this.buffers.delete(id); }
	voiceNew() {
		const id = this.nextId++;
		const gain = this.ctx.createGain();
		gain.connect(this.master);
		this.voices.set(id, { gain, src: null, playing: false });
		return id;
	}
	voiceFree(id) { this.stop(id); this.voices.get(id)?.gain.disconnect(); this.voices.delete(id); }
	play(id, buf, loop, vol, pitch) {
		const v = this.voices.get(id);
		const b = this.buffers.get(buf);
		if (!v) return;
		this.stop(id);
		if (!b) return;	// still decoding; the game retriggers short sounds and restarts loops on its own
		const src = this.ctx.createBufferSource();
		src.buffer = b;
		src.loop = !!loop;
		src.playbackRate.value = pitch > 0 ? pitch : 1;
		src.connect(v.gain);
		v.gain.gain.value = vol;
		src.onended = () => { if (v.src === src) v.playing = false; };
		src.start();
		v.src = src;
		v.playing = true;
	}
	stop(id) {
		const v = this.voices.get(id);
		if (!v?.src) return;
		try { v.src.stop(); } catch {}
		v.src.disconnect();
		v.src = null;
		v.playing = false;
	}
	playing(id) { return this.voices.get(id)?.playing ? 1 : 0; }
	set(id, vol, pitch) {
		const v = this.voices.get(id);
		if (!v) return;
		v.gain.gain.value = vol;
		if (v.src && pitch > 0) v.src.playbackRate.value = pitch;
	}
	globalVolume(vol) { this.master.gain.value = vol; }
	// Music streams from the server; the game only has an empty placeholder file of the same name.
	musicOpen(p) {
		p = p.replace(/\\/g, '/').replace(/^(\.\/|\/data\/|\/)/, '');
		this.music.src = 'data/' + p.split('/').map(encodeURIComponent).join('/');
		this.musicPaused = false;
	}
	musicCmd(cmd) {	// 0 stop, 1 play, 2 pause
		const m = this.music;
		if (cmd === 1) { this.musicPaused = false; m.play().catch(() => {}); }
		else if (cmd === 2) { this.musicPaused = true; m.pause(); }
		else { this.musicPaused = false; m.pause(); try { m.currentTime = 0; } catch {} }
	}
	musicStatus() {	// sf::SoundSource::Status: 0 stopped, 1 paused, 2 playing
		const m = this.music;
		if (!m.src || m.ended) return 0;
		if (m.paused) return this.musicPaused ? 1 : 0;
		return 2;
	}
	musicVolume(v) { this.musicVol = v; this.music.volume = Math.max(0, Math.min(1, v)); }
}

// --- filesystem: the game mixes upper and lower case and uses backslashes, as on Windows ---------------------------
function makeFsLikeWindows(FS) {
	const slash = (p) => (typeof p === 'string' ? p.replace(/\\/g, '/') : p);
	const lookupNode = FS.lookupNode;
	FS.lookupNode = function (parent, name) {
		try {
			return lookupNode.call(FS, parent, name);
		} catch (e) {
			const kids = parent.contents;
			if (kids && typeof kids === 'object' && !ArrayBuffer.isView(kids)) {
				const lower = name.toLowerCase();
				for (const k in kids) if (k.toLowerCase() === lower) return lookupNode.call(FS, parent, k);
			}
			throw e;
		}
	};
	for (const fn of ['lookupPath', 'mknod', 'mkdir', 'rmdir', 'unlink', 'stat', 'lstat', 'readdir', 'truncate', 'utime', 'chmod']) {
		const orig = FS[fn];
		FS[fn] = function (p, ...rest) { return orig.call(FS, slash(p), ...rest); };
	}
	const rename = FS.rename;
	FS.rename = (a, b) => rename.call(FS, slash(a), slash(b));
	const symlink = FS.symlink;
	FS.symlink = (a, b) => symlink.call(FS, slash(a), slash(b));
}

// --- game data ---------------------------------------------------------------------------------------------------
async function loadData(FS) {
	const res = await fetch('data/manifest.json', { cache: 'no-cache' });
	if (!res.ok) throw new Error('cannot load data/manifest.json (' + res.status + ')');
	const files = (await res.json()).files;
	let cache = null;
	try { cache = await caches.open('treadmarks-data-1'); } catch {}
	const wanted = new Set();
	const urlOf = (f) => 'data/' + f.p.split('/').map(encodeURIComponent).join('/') + '?v=' + f.t;
	const total = files.reduce((n, f) => n + (/\.ogg$/i.test(f.p) ? 0 : f.s), 0);
	let loaded = 0, fromNet = 0;
	const dirs = new Set();
	for (const f of files) {
		const d = f.p.includes('/') ? f.p.slice(0, f.p.lastIndexOf('/')) : '';
		if (!dirs.has(d)) { FS.mkdirTree('/data/' + d); dirs.add(d); }
	}
	const queue = files.slice();
	async function worker() {
		for (let f; (f = queue.shift()); ) {
			const dest = '/data/' + f.p;
			if (/\.ogg$/i.test(f.p)) { FS.writeFile(dest, new Uint8Array(0)); continue; }
			const url = urlOf(f);
			wanted.add(new URL(url, location.href).href);
			let r = cache && (await cache.match(url));
			if (!r) {
				r = await fetch(url);
				if (!r.ok) throw new Error('cannot load ' + f.p + ' (' + r.status + ')');
				if (cache) { try { await cache.put(url, r.clone()); } catch { cache = null; } }
				fromNet += f.s;
			}
			FS.writeFile(dest, new Uint8Array(await r.arrayBuffer()));
			loaded += f.s;
			status(`Loading game data… ${Math.floor((loaded / total) * 100)}%  (${(loaded / 1048576).toFixed(0)} of ${(total / 1048576).toFixed(0)} MB)`);
		}
	}
	await Promise.all(Array.from({ length: 8 }, worker));
	// Drop cached files from older versions of the data.
	if (cache) {
		for (const req of await cache.keys()) if (!wanted.has(req.url)) cache.delete(req);
	}
	console.log(`treadmarks-vr: ${files.length} data files, ${(fromNet / 1048576).toFixed(0)} MB from the network`);
}

// Saved settings, ladders and scores (/save, see GetAppDataDir in shim/sfml_web.cpp) live in IndexedDB.
function syncSave(FS, populate) {
	return new Promise((resolve) => FS.syncfs(populate, (err) => { if (err) console.warn('treadmarks-vr: save sync', err); resolve(); }));
}

// --- start-up ----------------------------------------------------------------------------------------------------
async function start() {
	const audio = new GameAudio();
	const game = {};
	let canvasW = 640, canvasH = 480;

	const M = await TreadMarks({
		canvas,
		print: (t) => console.log(t),
		printErr: (t) => console.warn(t),
		onAbort: (what) => { status('The game stopped: ' + what); $('overlay').hidden = false; },
		tmvrAudio: audio,
		tmvrSetCanvasSize: (w, h) => {
			canvasW = w; canvasH = h;
			if (!game.xrActive?.()) { canvas.width = w; canvas.height = h; }
			fitCanvas();
		},
		tmvrDesktopSize: () => isHeadset ? [1280, 960]
			: [Math.min(1920, Math.round(innerWidth * devicePixelRatio)), Math.min(1200, Math.round(innerHeight * devicePixelRatio))],
		tmvrFrameDone: () => {},
	});
	const FS = M.FS;
	makeFsLikeWindows(FS);
	FS.mkdirTree('/save');
	FS.mount(FS.filesystems.IDBFS, {}, '/save');
	await syncSave(FS, true);
	for (const d of ['/save/user', '/save/common', '/save/common/scores']) FS.mkdirTree(d);

	await loadData(FS);
	FS.chdir('/data');

	status(isHeadset ? 'Ready.' : 'Ready. Click to play.');
	$('play').hidden = false;
	await new Promise((resolve) => $('play').addEventListener('click', resolve, { once: true }));
	audio.resume();
	$('overlay').hidden = true;

	setupInput(M, audio);
	status('Starting…');
	M.callMain([]);

	setInterval(() => syncSave(FS, false), 10000);
	addEventListener('pagehide', () => syncSave(FS, false));

	// Flat screen: one game frame per display frame. In VR the headset's frame loop takes over (xr.js).
	let running = true;
	const frame = () => {
		try {
			input.beforeFrame();
			M._tmvr_frame();
		} catch (e) {
			running = false;
			status('The game stopped: ' + e);
			$('overlay').hidden = false;
			throw e;
		}
	};
	const loop = () => { if (running && !game.xrActive?.()) { frame(); } if (running) requestAnimationFrame(loop); };
	requestAnimationFrame(loop);
	Object.assign(game, setupXR({ M, canvas, input, frame, gameSize: () => [canvasW, canvasH], button: $('vr') }));
}

function fitCanvas() {
	const s = Math.min(innerWidth / canvas.width, innerHeight / canvas.height);
	canvas.style.width = Math.floor(canvas.width * s) + 'px';
	canvas.style.height = Math.floor(canvas.height * s) + 'px';
}
addEventListener('resize', fitCanvas);

// --- input -------------------------------------------------------------------------------------------------------
// Shared with xr.js: anything that "holds" a key (gamepad, VR controller) goes through setVirtual so a key held on
// both a gamepad and the keyboard is only released when neither holds it.
const input = {
	M: null,
	captured: false,	// the game is in a race and recentres the mouse each frame (mouse look)
	dx: 0, dy: 0, moved: false,
	held: new Map(),	// sf key -> set of holders
	mouseHeld: new Map(),
	beforeFrame() {},
	setVirtual(holder, keys) {	// keys: Set of sf key codes this holder wants down now
		for (const [k, who] of this.held) {
			if (who.has(holder) && !keys.has(k)) { who.delete(holder); if (!who.size) { this.held.delete(k); this.M._tmvr_key(k, 0, 0, 0, 0); } }
		}
		for (const k of keys) {
			let who = this.held.get(k);
			if (!who) { who = new Set(); this.held.set(k, who); this.M._tmvr_key(k, 1, 0, 0, 0); }
			who.add(holder);
		}
	},
	setMouseButton(holder, button, down) {
		let who = this.mouseHeld.get(button);
		if (!who) this.mouseHeld.set(button, (who = new Set()));
		const was = who.size > 0;
		if (down) who.add(holder); else who.delete(holder);
		if (was !== who.size > 0) this.M._tmvr_mouse_button(button, who.size > 0 ? 1 : 0, this.lastX ?? 0, this.lastY ?? 0);
	},
	mouseTo(x, y) { this.lastX = x; this.lastY = y; this.M._tmvr_mouse_move(x, y); },
	KEY,
};

function setupInput(M, audio) {
	input.M = M;
	const mods = (e) => [e.altKey ? 1 : 0, e.ctrlKey ? 1 : 0, e.shiftKey ? 1 : 0];
	addEventListener('keydown', (e) => {
		audio.resume();
		if (BROWSER_KEYS.has(e.code)) return;
		const k = KEY[e.code];
		if (k !== undefined) { M._tmvr_key(k, 1, ...mods(e)); e.preventDefault(); }
		if (e.key.length === 1 && !e.ctrlKey && !e.altKey) M._tmvr_text(e.key.charCodeAt(0));
		else if (e.code === 'Backspace') M._tmvr_text(8);
		else if (e.code === 'Enter' || e.code === 'NumpadEnter') M._tmvr_text(13);
	});
	addEventListener('keyup', (e) => {
		const k = KEY[e.code];
		if (k !== undefined) { M._tmvr_key(k, 0, ...mods(e)); e.preventDefault(); }
	});
	addEventListener('blur', () => M._tmvr_focus(0));
	addEventListener('focus', () => M._tmvr_focus(1));

	const toGame = (e) => {
		const r = canvas.getBoundingClientRect();
		return [Math.round(((e.clientX - r.left) * canvas.width) / r.width), Math.round(((e.clientY - r.top) * canvas.height) / r.height)];
	};
	canvas.addEventListener('mousemove', (e) => {
		if (document.pointerLockElement === canvas) { input.dx += e.movementX; input.dy += e.movementY; input.moved = true; }
		else input.mouseTo(...toGame(e));
	});
	const buttonOf = (b) => (b === 0 ? 0 : b === 2 ? 1 : b === 1 ? 2 : -1);	// browser left/middle/right -> sf::Mouse
	canvas.addEventListener('mousedown', (e) => {
		audio.resume();
		if (input.captured && document.pointerLockElement !== canvas) canvas.requestPointerLock?.();
		const b = buttonOf(e.button);
		if (b >= 0) input.setMouseButton('mouse', b, true);
		e.preventDefault();
	});
	addEventListener('mouseup', (e) => { const b = buttonOf(e.button); if (b >= 0) input.setMouseButton('mouse', b, false); });
	canvas.addEventListener('contextmenu', (e) => e.preventDefault());

	input.beforeFrame = () => {
		// While racing, the game reads mouse motion as distance from the window centre and then recentres it.
		input.captured = !!M._tmvr_mouse_captured();
		if (!input.captured && document.pointerLockElement === canvas) document.exitPointerLock();
		if (input.captured && input.moved) {
			input.mouseTo(Math.round(canvas.width / 2 + input.dx), Math.round(canvas.height / 2 + input.dy));
			input.dx = input.dy = 0; input.moved = false;
		}
		pollGamepads();
	};
}

// A normal gamepad (also the Legion Go's own controls) drives the default keyboard controls, so nothing needs
// setting up in the game's options.
export function padKeys(axes, out) {	// axes: left x, left y, right x
	const [lx, ly, rx] = axes;
	if (ly < -0.5) out.add(KEY.ArrowUp);
	if (ly > 0.5) out.add(KEY.ArrowDown);
	if (lx < -0.5) out.add(KEY.ArrowLeft);
	if (lx > 0.5) out.add(KEY.ArrowRight);
	if (rx < -0.5) out.add(KEY.KeyZ);	// gun left
	if (rx > 0.5) out.add(KEY.KeyC);	// gun right
	return out;
}

function pollGamepads() {
	const keys = new Set();
	for (const pad of navigator.getGamepads?.() || []) {
		if (!pad || pad.mapping !== 'standard') continue;
		const b = (i) => !!pad.buttons[i]?.pressed;
		padKeys(pad.axes, keys);
		if (b(12)) keys.add(KEY.ArrowUp);
		if (b(13)) keys.add(KEY.ArrowDown);
		if (b(14)) keys.add(KEY.ArrowLeft);
		if (b(15)) keys.add(KEY.ArrowRight);
		if (b(0)) keys.add(input.captured ? KEY.Space : KEY.Enter);	// A: fire in a race, select in menus
		if (b(7) || b(5)) keys.add(KEY.Space);	// right trigger / bumper: fire
		if (b(1) || b(9)) keys.add(KEY.Escape);	// B / Start
		if (b(6)) keys.add(KEY.Tab);	// left trigger: scores
		if (b(3)) keys.add(KEY.KeyN);	// Y: look back
		if (b(4)) keys.add(KEY.Home);	// left bumper: reset camera
	}
	input.setVirtual('gamepad', keys);
}

start().catch((e) => { console.error(e); status('Could not start: ' + e.message); });
