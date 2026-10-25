// treadmarks-vr: WebXR. The game runs unchanged, once per headset frame; shim/gl1.cpp draws the 3D world once per
// eye (the game's own chase camera, plus head tracking) and the 2D screen (menus, HUD) onto a panel in front of the
// player. This file fills the XrFrame struct that gl1.cpp reads (layout must match shim/gl1.cpp) and maps the
// Touch controllers to the game's default keyboard and mouse controls.

import { padKeys } from './platform.js';

// struct XrFrame offsets (bytes), see shim/gl1.cpp
const VIEW0 = 16, VIEW_SIZE = 272, V_VP = 0, V_PROJ = 16, V_EYE = 80, V_HUDPROJ = 144, V_HUDVIEW = 208;
const HUD_MODEL = 560, HUD_SIZE = 624, HUD_OPACITY = 632;

// The panel: a virtual screen 2 m wide, 2.2 m in front of where the player's head was when VR started.
const PANEL_DIST = 2.2, PANEL_W = 2.0, PANEL_Y = -0.15;

export function setupXR({ M, canvas, input, frame, gameSize, button }) {
	let session = null, refSpace = null, fboId = 0;
	const gl = () => M.GL.currentContext.GLctx;

	if (navigator.xr) {
		navigator.xr.isSessionSupported('immersive-vr').then((ok) => {
			if (!ok) return;
			button.hidden = false;
			button.addEventListener('click', () => (session ? session.end() : enter()));
		});
	}

	async function enter() {
		try {
			session = await navigator.xr.requestSession('immersive-vr', { optionalFeatures: ['local-floor'] });
		} catch (e) {
			console.warn('treadmarks-vr: VR not started', e);
			return;
		}
		const ctx = gl();
		await ctx.makeXRCompatible();
		const layer = new XRWebGLLayer(session, ctx, { antialias: true, depth: true, framebufferScaleFactor: 1.0 });
		session.updateRenderState({ baseLayer: layer, depthNear: 0.1, depthFar: 1000 });
		// 'local': the origin is where the head is at the start, which is where the game's camera is.
		refSpace = await session.requestReferenceSpace('local');
		refSpace.addEventListener?.('reset', () => {});
		// Let gl1.cpp bind the headset's framebuffer by number, like any other.
		fboId = M.GL.getNewId(M.GL.framebuffers);
		layer.framebuffer.name = fboId;
		M.GL.framebuffers[fboId] = layer.framebuffer;
		button.textContent = 'Exit VR';
		session.addEventListener('end', () => {
			const p = M._tmgl_XrFrame();
			M.HEAP32[p >> 2] = 0;
			M.GL.framebuffers[fboId] = null;
			session = null;
			button.textContent = 'Enter VR';
			input.setVirtual('xr', new Set());
			input.setMouseButton('xr', 0, false);
		});
		session.requestAnimationFrame(onFrame);
	}

	const panelSize = () => {
		const [w, h] = gameSize();
		return [PANEL_W, (PANEL_W * h) / Math.max(1, w)];
	};

	function onFrame(t, xrFrame) {
		if (!session) return;
		session.requestAnimationFrame(onFrame);
		const pose = xrFrame.getViewerPose(refSpace);
		if (!pose) return;
		const layer = session.renderState.baseLayer;
		const p = M._tmgl_XrFrame();
		const I = M.HEAP32, F = M.HEAPF32;
		const views = pose.views.slice(0, 2);
		I[p >> 2] = 1;
		I[(p >> 2) + 1] = fboId;
		I[(p >> 2) + 2] = views.length;
		views.forEach((view, i) => {
			const base = p + VIEW0 + i * VIEW_SIZE;
			const vp = layer.getViewport(view);
			I.set([vp.x, vp.y, vp.width, vp.height], (base + V_VP) >> 2);
			F.set(view.projectionMatrix, (base + V_PROJ) >> 2);
			F.set(view.transform.inverse.matrix, (base + V_EYE) >> 2);
			F.set(view.projectionMatrix, (base + V_HUDPROJ) >> 2);
			F.set(view.transform.inverse.matrix, (base + V_HUDVIEW) >> 2);
		});
		const [pw, ph] = panelSize();
		F.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, PANEL_Y, -PANEL_DIST, 1], (p + HUD_MODEL) >> 2);
		F.set([pw, ph], (p + HUD_SIZE) >> 2);
		F[(p + HUD_OPACITY) >> 2] = 1;

		controllers(xrFrame, pw, ph);
		frame();
	}

	// Right controller: point at the panel to move the mouse in menus, trigger = left mouse button (also fire, the
	// game's second binding). Sticks and buttons = the game's default keys, as for a gamepad (platform.js).
	function controllers(xrFrame, pw, ph) {
		const keys = new Set();
		let trigger = false;
		for (const src of session.inputSources) {
			const pad = src.gamepad;
			if (!pad) continue;
			const b = (i) => !!pad.buttons[i]?.pressed;
			const ax = pad.axes.length >= 4 ? [pad.axes[2], pad.axes[3]] : [pad.axes[0] || 0, pad.axes[1] || 0];
			if (src.handedness === 'left') {
				padKeys([ax[0], ax[1], 0], keys);
				if (b(0)) keys.add(input.KEY.Space);	// left trigger: fire too
				if (b(4)) keys.add(input.KEY.Tab);	// X: scores
				if (b(5)) keys.add(input.KEY.KeyN);	// Y: look back
				if (b(3)) keys.add(input.KEY.Home);	// stick click: reset camera
			} else {
				padKeys([0, 0, ax[0]], keys);
				if (ax[1] < -0.5) keys.add(input.KEY.Insert);	// tilt camera up / down
				if (ax[1] > 0.5) keys.add(input.KEY.Delete);
				if (b(0)) trigger = true;
				if (b(4)) keys.add(input.captured ? input.KEY.Space : input.KEY.Enter);	// A
				if (b(5)) keys.add(input.KEY.Escape);	// B
				if (b(1)) keys.add(input.KEY.KeyM);	// grip: turret camera
				if (!input.captured) pointAtPanel(xrFrame, src, pw, ph);
			}
		}
		input.setVirtual('xr', keys);
		input.setMouseButton('xr', 0, trigger);
	}

	function pointAtPanel(xrFrame, src, pw, ph) {
		const pose = xrFrame.getPose(src.targetRaySpace, refSpace);
		if (!pose) return;
		const m = pose.transform.matrix;
		const o = [m[12], m[13], m[14]], d = [-m[8], -m[9], -m[10]];	// ray points down -Z
		if (d[2] >= -1e-4) return;
		const s = (-PANEL_DIST - o[2]) / d[2];
		const x = o[0] + d[0] * s, y = o[1] + d[1] * s - PANEL_Y;
		const u = x / pw + 0.5, v = 0.5 - y / ph;
		if (u < 0 || u > 1 || v < 0 || v > 1) return;
		const [w, h] = gameSize();
		input.mouseTo(Math.round(u * w), Math.round(v * h));
	}

	// ?fakexr: fills the VR frame data with two side-by-side eyes on the flat canvas, so the stereo path and the panel
	// can be checked without a headset.
	// window.tmvrFakeXr toggles it while running.
	window.tmvrFakeXr = new URLSearchParams(location.search).has('fakexr');
	function fakeFrame() {
		if (!window.tmvrFakeXr) { if (!session) M.HEAP32[M._tmgl_XrFrame() >> 2] = 0; return; }
		const p = M._tmgl_XrFrame();
		const I = M.HEAP32, F = M.HEAPF32;
		const w = canvas.width, h = canvas.height, n = 0.1, f = 1000, t = Math.tan(Math.PI / 4);
		const a = w / 2 / h;
		const proj = [1 / (t * a), 0, 0, 0, 0, 1 / t, 0, 0, 0, 0, -(f + n) / (f - n), -1, 0, 0, (-2 * f * n) / (f - n), 0];
		I[p >> 2] = 1; I[(p >> 2) + 1] = 0; I[(p >> 2) + 2] = 2;
		[[0, 0.032], [w / 2, -0.032]].forEach(([x, shift], i) => {
			const base = p + VIEW0 + i * VIEW_SIZE;
			const eye = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, shift, 0, 0, 1];
			I.set([x, 0, w / 2, h], (base + V_VP) >> 2);
			F.set(proj, (base + V_PROJ) >> 2);
			F.set(eye, (base + V_EYE) >> 2);
			F.set(proj, (base + V_HUDPROJ) >> 2);
			F.set(eye, (base + V_HUDVIEW) >> 2);
		});
		const [pw, ph] = panelSize();
		F.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, PANEL_Y, -PANEL_DIST, 1], (p + HUD_MODEL) >> 2);
		F.set([pw, ph], (p + HUD_SIZE) >> 2);
		F[(p + HUD_OPACITY) >> 2] = 1;
	}

	return { xrActive: () => !!session, beforeFrame: () => { if (!session) fakeFrame(); } };
}
