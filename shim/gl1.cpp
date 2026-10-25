// treadmarks-vr: OpenGL 1.x fixed-function pipeline for Tread Marks, on WebGL 2.
//
// The game draws in immediate mode (glBegin/glVertex/glEnd). Calling WebGL once per vertex would be far too slow on a
// Quest, so this file collects the vertices of the whole frame in one array, turns quads, fans and strips into
// triangles, and records a list of draws (each with its render state and matrices). At the end of the frame
// (tmgl_EndFrame, called from the buffer swap) the array is uploaded once and the draws are replayed:
//   - flat screen: in order, exactly as the game issued them;
//   - VR: the 3D world draws once per eye with the eye's projection and an extra eye transform, and the 2D screen
//     (HUD, menus) once into a texture that is shown on a panel in front of the player.
// The game's own camera, culling and level of detail run once per frame, unchanged.

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
#define TMGL_IMPL
#include "GL/glew.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

// OpenGL 1 enum values that WebGL's headers don't have.
enum {
	K_QUADS = 0x0007, K_QUAD_STRIP = 0x0008, K_POLYGON = 0x0009,
	K_LIGHTING = 0x0B50, K_FOG = 0x0B60, K_FOG_DENSITY = 0x0B62, K_FOG_START = 0x0B63, K_FOG_END = 0x0B64,
	K_FOG_MODE = 0x0B65, K_FOG_COLOR = 0x0B66, K_ALPHA_TEST = 0x0BC0, K_EXP = 0x0800, K_EXP2 = 0x0801,
	K_MODELVIEW = 0x1700, K_PROJECTION = 0x1701, K_TEXTURE_MTX = 0x1702,
	K_MODULATE = 0x2100, K_DECAL = 0x2101, K_ADD = 0x0104, K_TEXTURE_ENV_MODE = 0x2200,
	K_CLAMP = 0x2900, K_TEXTURE_WIDTH = 0x1000, K_TEXTURE_HEIGHT = 0x1001,
	K_UNPACK_SWAP_BYTES = 0x0CF0, K_UNPACK_LSB_FIRST = 0x0CF1,
	K_R3_G3_B2 = 0x2A10, K_RGB4 = 0x804F, K_RGB5 = 0x8050, K_RGB8 = 0x8051, K_LUMINANCE = 0x1909
};

namespace {

struct Mat4 { float m[16]; };

void matIdentity(float* m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.0f; }
void matRotate(float* r, float angleDeg, float x, float y, float z) {	// as glRotatef
	float len = sqrtf(x * x + y * y + z * z);
	matIdentity(r);
	if (len <= 0.0f) return;
	x /= len; y /= len; z /= len;
	float a = angleDeg * 3.14159265358979f / 180.0f, c = cosf(a), s = sinf(a), ic = 1.0f - c;
	r[0] = x * x * ic + c; r[1] = y * x * ic + z * s; r[2] = x * z * ic - y * s;
	r[4] = x * y * ic - z * s; r[5] = y * y * ic + c; r[6] = y * z * ic + x * s;
	r[8] = x * z * ic + y * s; r[9] = y * z * ic - x * s; r[10] = z * z * ic + c;
}
void matMul(float* out, const float* a, const float* b) {	// out = a * b (column-major)
	float r[16];
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++)
			r[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] + a[8 + rr] * b[c * 4 + 2] + a[12 + rr] * b[c * 4 + 3];
	memcpy(out, r, 64);
}

struct Vertex {
	float pos[3];
	unsigned char col[4];
	float uv0[2];
	float uv1[2];
};

// Everything that affects how a draw looks. Compared with memcmp, so always built by copying `cur`.
struct DrawState {
	unsigned char tex2d[2], env[2];	// env: 0 modulate, 1 replace, 2 decal, 3 add
	unsigned char blend, depthTest, depthMask, cull, fog, alphaTest, polyOffset, phase;
	GLuint tex[2];
	GLenum blendSrc, blendDst, depthFunc, cullFace, frontFace, alphaFunc, fogMode;
	float alphaRef, fogStart, fogEnd, fogDensity, fogColor[4], polyFactor, polyUnits;
	int vp[4];
};

enum { CMD_DRAW, CMD_CLEAR };
struct Cmd {
	int type;
	GLenum prim;	// GL_TRIANGLES, GL_LINES or GL_POINTS
	int first, count;
	int state, mv, proj;
	GLbitfield clearMask;
	float clearColor[4];
};

struct TexInfo { int w, h, maxLevel; bool alpha; };

// What the page gives us for a VR frame (web/main.js fills it through tmgl_XrFrame()).
struct XrView { int vp[4]; float proj[16]; float eye[16]; float hudProj[16]; float hudView[16]; };
struct XrFrame {
	int active, fbo, nviews, pad;
	XrView views[2];
	float hudModel[16];	// panel placement (metres, reference space); the panel is a unit quad scaled by hudSize
	float hudSize[2];
	float hudOpacity, pad2;
};

DrawState cur;
float curColor[4] = {1, 1, 1, 1};
float curUV[2][2];
int activeUnit = 0;
GLenum matMode = K_MODELVIEW;
std::vector<Mat4> stack[3];
bool mvDirty = true, projDirty = true;
GLenum primMode;
bool inBegin = false;
std::vector<Vertex> primVerts;
float clearColor[4] = {0, 0, 0, 0};
int unpackRowLength = 0, unpackSkipRows = 0, unpackSkipPixels = 0, unpackAlignment = 4;
std::unordered_map<GLuint, TexInfo> textures;
std::vector<GLuint> pendingDeletes;
std::vector<unsigned char> scratch;

std::vector<Vertex> verts;
std::vector<Cmd> cmds;
std::vector<DrawState> states;
std::vector<Mat4> mats;
int lastMv = -1, lastProj = -1;

XrFrame xr;
float foldYaw = 0.0f, foldPitch = 0.0f;	// see tmgl_FoldHeadYaw
int width = 640, height = 480;

GLuint prog, vbo, vao, hudProg, hudVbo, hudVao, hudFbo, hudTex, hudDepth;
int hudW = 0, hudH = 0;
struct {
	GLint P, E, MV, T0, T1, Tex0, Tex1, AlphaFunc, AlphaRef, Fog, FogParams, FogColor, Opaque;
} loc;
struct { GLint MVP, T, Opacity; } hudLoc;

Mat4& top(GLenum mode) {
	int i = mode == K_PROJECTION ? 1 : mode == K_TEXTURE_MTX ? 2 : 0;
	return stack[i].back();
}
void markDirty() {
	if (matMode == K_MODELVIEW) mvDirty = true;
	else if (matMode == K_PROJECTION) projDirty = true;
}

const char* kVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;
layout(location=2) in vec2 aUV0;
layout(location=3) in vec2 aUV1;
uniform mat4 uP, uE, uMV;
out vec4 vCol; out vec2 vUV0; out vec2 vUV1; out float vFogZ;
void main() {
	vec4 e = uMV * vec4(aPos, 1.0);
	vFogZ = abs(e.z);
	vCol = aCol; vUV0 = aUV0; vUV1 = aUV1;
	gl_Position = uP * (uE * e);
	gl_PointSize = 1.0;
})";

const char* kFS = R"(#version 300 es
precision mediump float;
in vec4 vCol; in vec2 vUV0; in vec2 vUV1; in float vFogZ;
uniform sampler2D uT0, uT1;
uniform int uTex0, uTex1, uAlphaFunc, uFog, uOpaque;
uniform float uAlphaRef;
uniform vec3 uFogParams;
uniform vec4 uFogColor;
out vec4 frag;
vec4 env(vec4 c, vec4 t, int mode) {
	if (mode == 1) return c * t;
	if (mode == 2) return t;
	if (mode == 3) return vec4(mix(c.rgb, t.rgb, t.a), c.a);
	if (mode == 4) return vec4(c.rgb + t.rgb, c.a * t.a);
	return c;
}
void main() {
	vec4 c = vCol;
	if (uTex0 != 0) c = env(c, texture(uT0, vUV0), uTex0);
	if (uTex1 != 0) c = env(c, texture(uT1, vUV1), uTex1);
	if (uAlphaFunc != 0) {
		bool pass = true;
		if (uAlphaFunc == 1) pass = false;
		else if (uAlphaFunc == 2) pass = c.a < uAlphaRef;
		else if (uAlphaFunc == 3) pass = c.a == uAlphaRef;
		else if (uAlphaFunc == 4) pass = c.a <= uAlphaRef;
		else if (uAlphaFunc == 5) pass = c.a > uAlphaRef;
		else if (uAlphaFunc == 6) pass = c.a != uAlphaRef;
		else if (uAlphaFunc == 7) pass = c.a >= uAlphaRef;
		if (!pass) discard;
	}
	if (uFog != 0) {
		float f;
		if (uFog == 1) f = (uFogParams.y - vFogZ) / max(uFogParams.y - uFogParams.x, 0.0001);
		else if (uFog == 2) f = exp(-uFogParams.z * vFogZ);
		else { float d = uFogParams.z * vFogZ; f = exp(-d * d); }
		c.rgb = mix(uFogColor.rgb, c.rgb, clamp(f, 0.0, 1.0));
	}
	if (uOpaque != 0) c.a = 1.0;	// drawn without blending onto the VR panel: opaque, as it looks on a flat screen
	frag = c;
})";

const char* kHudVS = R"(#version 300 es
layout(location=0) in vec2 aPos;
uniform mat4 uMVP;
out vec2 vUV;
void main() { vUV = aPos + 0.5; gl_Position = uMVP * vec4(aPos, 0.0, 1.0); })";

const char* kHudFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uT;
uniform float uOpacity;
out vec4 frag;
void main() { frag = texture(uT, vUV) * uOpacity; })";

GLuint compile(GLenum type, const char* src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		printf("tmgl: shader error: %s\n", log);
	}
	return s;
}
GLuint link(const char* vs, const char* fs) {
	GLuint p = glCreateProgram();
	glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetProgramInfoLog(p, sizeof(log), nullptr, log);
		printf("tmgl: link error: %s\n", log);
	}
	return p;
}

void resetCur() {
	memset(&cur, 0, sizeof(cur));
	cur.depthMask = 1;
	cur.blendSrc = GL_ONE; cur.blendDst = GL_ZERO;
	cur.depthFunc = GL_LESS;
	cur.cullFace = GL_BACK; cur.frontFace = GL_CCW;
	cur.alphaFunc = GL_ALWAYS;
	cur.fogMode = K_EXP; cur.fogEnd = 1.0f; cur.fogDensity = 1.0f;
	cur.env[0] = cur.env[1] = 0;
	cur.phase = TMGL_PHASE_HUD;
	cur.vp[2] = width; cur.vp[3] = height;
}

int stateIndex() {
	if (states.empty() || memcmp(&states.back(), &cur, sizeof(cur)) != 0) states.push_back(cur);
	return (int)states.size() - 1;
}
int matIndex(bool& dirty, int& last, GLenum mode) {
	if (dirty || last < 0) {
		mats.push_back(top(mode));
		last = (int)mats.size() - 1;
		dirty = false;
	}
	return last;
}

// Appends finished triangles/lines/points to the frame, merging with the previous draw when nothing changed.
void emit(GLenum prim, const Vertex* v, int n) {
	if (n <= 0) return;
	int st = stateIndex();
	int mv = matIndex(mvDirty, lastMv, K_MODELVIEW);
	int pj = matIndex(projDirty, lastProj, K_PROJECTION);
	int first = (int)verts.size();
	verts.insert(verts.end(), v, v + n);
	if (!cmds.empty()) {
		Cmd& c = cmds.back();
		if (c.type == CMD_DRAW && c.prim == prim && c.state == st && c.mv == mv && c.proj == pj && c.first + c.count == first) {
			c.count += n;
			return;
		}
	}
	Cmd c = {};
	c.type = CMD_DRAW; c.prim = prim; c.first = first; c.count = n; c.state = st; c.mv = mv; c.proj = pj;
	cmds.push_back(c);
}

std::vector<Vertex> tri;
void finishPrimitive() {
	const std::vector<Vertex>& p = primVerts;
	int n = (int)p.size();
	tri.clear();
	switch (primMode) {
	case GL_TRIANGLES:
		emit(GL_TRIANGLES, p.data(), n - n % 3);
		return;
	case GL_TRIANGLE_STRIP:
	case K_QUAD_STRIP:
		for (int i = 2; i < n; i++) {
			if (primMode == K_QUAD_STRIP || (i & 1) == 0) { tri.push_back(p[i - 2]); tri.push_back(p[i - 1]); tri.push_back(p[i]); }
			else { tri.push_back(p[i - 1]); tri.push_back(p[i - 2]); tri.push_back(p[i]); }
			if (primMode == K_QUAD_STRIP) {	// quad strip: (i-2, i-1, i) and (i-1, i+1, i) per pair
				if (i + 1 < n) { tri.push_back(p[i - 1]); tri.push_back(p[i + 1]); tri.push_back(p[i]); }
				i++;
			}
		}
		break;
	case GL_TRIANGLE_FAN:
	case K_POLYGON:
		for (int i = 2; i < n; i++) { tri.push_back(p[0]); tri.push_back(p[i - 1]); tri.push_back(p[i]); }
		break;
	case K_QUADS:
		for (int i = 0; i + 3 < n; i += 4) {
			tri.push_back(p[i]); tri.push_back(p[i + 1]); tri.push_back(p[i + 2]);
			tri.push_back(p[i]); tri.push_back(p[i + 2]); tri.push_back(p[i + 3]);
		}
		break;
	case GL_LINES:
		emit(GL_LINES, p.data(), n & ~1);
		return;
	case GL_POINTS:
		emit(GL_POINTS, p.data(), n);
		return;
	default:
		return;
	}
	emit(GL_TRIANGLES, tri.data(), (int)tri.size());
}

int alphaFuncIndex(GLenum f) {
	switch (f) {
	case GL_NEVER: return 1; case GL_LESS: return 2; case GL_EQUAL: return 3; case GL_LEQUAL: return 4;
	case GL_GREATER: return 5; case GL_NOTEQUAL: return 6; case GL_GEQUAL: return 7; default: return 0;
	}
}

// --- replay -------------------------------------------------------------------------------------------------------

bool applied = false;
DrawState last;
int lastOpaque = -1;
void applyState(const DrawState& s, bool force, bool hudAlpha) {
	// On a flat screen alpha is ignored when blending is off, and the game's opaque art doesn't always have alpha 1.
	int opaque = hudAlpha && !s.blend ? 1 : 0;
	if (force || opaque != lastOpaque) { glUniform1i(loc.Opaque, opaque); lastOpaque = opaque; }
	if (force || s.blend != last.blend) { if (s.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND); }
	if (force || s.blendSrc != last.blendSrc || s.blendDst != last.blendDst || hudAlpha) {
		// Drawing the 2D layer into its own (transparent) texture in VR: keep alpha as coverage so the panel can be
		// blended over the world afterwards.
		if (hudAlpha) glBlendFuncSeparate(s.blendSrc, s.blendDst, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		else glBlendFunc(s.blendSrc, s.blendDst);
	}
	if (force || s.depthTest != last.depthTest) { if (s.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
	if (force || s.depthFunc != last.depthFunc) glDepthFunc(s.depthFunc);
	if (force || s.depthMask != last.depthMask) glDepthMask(s.depthMask);
	if (force || s.cull != last.cull) { if (s.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
	if (force || s.cullFace != last.cullFace) glCullFace(s.cullFace);
	if (force || s.frontFace != last.frontFace) glFrontFace(s.frontFace);
	if (force || s.polyOffset != last.polyOffset) { if (s.polyOffset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL); }
	if (force || s.polyFactor != last.polyFactor || s.polyUnits != last.polyUnits) glPolygonOffset(s.polyFactor, s.polyUnits);
	for (int u = 0; u < 2; u++) {
		if (force || s.tex[u] != last.tex[u]) { glActiveTexture(GL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, s.tex[u]); }
	}
	int t0 = s.tex2d[0] && s.tex[0] ? s.env[0] + 1 : 0;
	int t1 = s.tex2d[1] && s.tex[1] ? s.env[1] + 1 : 0;
	int lt0 = last.tex2d[0] && last.tex[0] ? last.env[0] + 1 : 0;
	int lt1 = last.tex2d[1] && last.tex[1] ? last.env[1] + 1 : 0;
	if (force || t0 != lt0) glUniform1i(loc.Tex0, t0);
	if (force || t1 != lt1) glUniform1i(loc.Tex1, t1);
	int af = s.alphaTest ? alphaFuncIndex(s.alphaFunc) : 0;
	int laf = last.alphaTest ? alphaFuncIndex(last.alphaFunc) : 0;
	if (force || af != laf) glUniform1i(loc.AlphaFunc, af);
	if (force || s.alphaRef != last.alphaRef) glUniform1f(loc.AlphaRef, s.alphaRef);
	int fog = s.fog ? (s.fogMode == GL_LINEAR ? 1 : s.fogMode == K_EXP ? 2 : 3) : 0;
	int lfog = last.fog ? (last.fogMode == GL_LINEAR ? 1 : last.fogMode == K_EXP ? 2 : 3) : 0;
	if (force || fog != lfog) glUniform1i(loc.Fog, fog);
	if (force || s.fogStart != last.fogStart || s.fogEnd != last.fogEnd || s.fogDensity != last.fogDensity)
		glUniform3f(loc.FogParams, s.fogStart, s.fogEnd, s.fogDensity);
	if (force || memcmp(s.fogColor, last.fogColor, 16) != 0) glUniform4fv(loc.FogColor, 1, s.fogColor);
	last = s;
}

// Projection with the game's near/far planes but the eye's field of view (WebXR matrices are in metres and their
// near/far come from the session; game units are metres too, so only the depth range needs replacing).
void eyeProjection(float* out, const float* eye, const float* game) {
	memcpy(out, eye, 64);
	float n = 1.0f, f = 1000.0f;
	if (game[15] == 0.0f && game[10] != -1.0f && game[10] != 1.0f) {
		n = game[14] / (game[10] - 1.0f);
		f = game[14] / (game[10] + 1.0f);
	}
	out[10] = -(f + n) / (f - n);
	out[14] = -2.0f * f * n / (f - n);
}

enum { REPLAY_ALL, REPLAY_WORLD, REPLAY_HUD };
// scale: from the game's pixel coordinates to the target's (viewport and scissor)
void replay(int which, const XrView* view, float sx, float sy, int ox, int oy, bool hudAlpha) {
	glUseProgram(prog);
	glBindVertexArray(vao);
	glUniform1i(loc.T0, 0);
	glUniform1i(loc.T1, 1);
	float ident[16];
	matIdentity(ident);
	glUniformMatrix4fv(loc.E, 1, GL_FALSE, view ? view->eye : ident);
	applied = false;
	int lastMvI = -1, lastPjI = -1;
	int vp[4] = {-1, -1, -1, -1};
	for (const Cmd& c : cmds) {
		const DrawState& s = states[c.type == CMD_DRAW ? c.state : c.state];
		bool world = s.phase == TMGL_PHASE_WORLD;
		// 2D drawn during the 3D pass (race countdown and the like) belongs on the panel in VR.
		if (world && c.type == CMD_DRAW && mats[c.proj].m[15] != 0.0f) world = false;
		if (which == REPLAY_WORLD && !world) continue;
		if (which == REPLAY_HUD && world) continue;
		if (!view && memcmp(vp, s.vp, sizeof(vp)) != 0) {
			memcpy(vp, s.vp, sizeof(vp));
			glViewport(ox + (int)(vp[0] * sx), oy + (int)(vp[1] * sy), (int)(vp[2] * sx), (int)(vp[3] * sy));
		}
		if (c.type == CMD_CLEAR) {
			GLbitfield mask = c.clearMask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			if (!s.depthMask) mask &= ~GL_DEPTH_BUFFER_BIT;
			if (mask) {
				glDepthMask(GL_TRUE);
				if (hudAlpha) glClearColor(c.clearColor[0], c.clearColor[1], c.clearColor[2], c.clearColor[3]);
				else glClearColor(c.clearColor[0], c.clearColor[1], c.clearColor[2], 1.0f);
				glClear(mask);
				glDepthMask(applied ? last.depthMask : GL_TRUE);
			}
			continue;
		}
		applyState(s, !applied, hudAlpha);
		applied = true;
		if (c.mv != lastMvI) { glUniformMatrix4fv(loc.MV, 1, GL_FALSE, mats[c.mv].m); lastMvI = c.mv; }
		if (c.proj != lastPjI) {
			if (view) {
				const float* gp = mats[c.proj].m;
				if (gp[15] != 0.0f) continue;	// a 2D draw inside the 3D pass: not shown in VR
				float p[16];
				eyeProjection(p, view->proj, gp);
				glUniformMatrix4fv(loc.P, 1, GL_FALSE, p);
			} else {
				glUniformMatrix4fv(loc.P, 1, GL_FALSE, mats[c.proj].m);
			}
			lastPjI = c.proj;
		}
		glDrawArrays(c.prim, c.first, c.count);
	}
}

void ensureHudTarget() {
	if (hudW == width && hudH == height) return;
	hudW = width; hudH = height;
	if (!hudFbo) {
		glGenFramebuffers(1, &hudFbo);
		glGenTextures(1, &hudTex);
		glGenRenderbuffers(1, &hudDepth);
	}
	glBindTexture(GL_TEXTURE_2D, hudTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hudW, hudH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindRenderbuffer(GL_RENDERBUFFER, hudDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, hudW, hudH);
	glBindFramebuffer(GL_FRAMEBUFFER, hudFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hudTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hudDepth);
}

void drawHudPanel(const XrView& v) {
	float mvp[16];
	matMul(mvp, v.hudView, xr.hudModel);
	float s[16];
	matIdentity(s);
	s[0] = xr.hudSize[0]; s[5] = xr.hudSize[1];
	matMul(mvp, mvp, s);
	matMul(mvp, v.hudProj, mvp);
	glUseProgram(hudProg);
	glBindVertexArray(hudVao);
	glUniformMatrix4fv(hudLoc.MVP, 1, GL_FALSE, mvp);
	glUniform1f(hudLoc.Opacity, xr.hudOpacity);
	glUniform1i(hudLoc.T, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, hudTex);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	applied = false;
	last.tex[0] = hudTex;
}

void resetFrame() {
	verts.clear();
	cmds.clear();
	states.clear();
	mats.clear();
	lastMv = lastProj = -1;
	mvDirty = projDirty = true;
	cur.phase = TMGL_PHASE_HUD;
	foldYaw = 0.0f;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void* tmgl_XrFrame() { return &xr; }
EMSCRIPTEN_KEEPALIVE int tmgl_Width() { return width; }
EMSCRIPTEN_KEEPALIVE int tmgl_Height() { return height; }

// Headset field of view plus a margin (see the treadmarks-vr note in TankGame.cpp). Turning the head sideways is
// covered by tmgl_FoldHeadYaw instead.
static float cullAngle = 120.0f;
EMSCRIPTEN_KEEPALIVE void tmgl_SetCullAngle(float degrees) { cullAngle = degrees; }
float tmgl_CullAngle(void) { return xr.active && xr.nviews > 0 ? cullAngle : 0.0f; }

// In VR the game draws (and culls) its world with its camera turned by the head's yaw, so whatever the player turns
// to look at has been prepared. The game's camera is pitch * yaw * position (glRotatef a about X, then b about Y), so
// turning the yaw by d and using eye * Rx(a) * Ry(-d) * Rx(-a) as the eye matrix gives exactly the same picture.
float tmgl_FoldHeadYaw(float camPitch) {
	if (!(xr.active && xr.nviews > 0)) return 0.0f;
	const float* v = xr.views[0].eye;	// view matrix; the head looks along -(v[2], v[6], v[10])
	foldYaw = -atan2f(v[2], v[10]) * 180.0f / 3.14159265358979f;
	foldPitch = camPitch;
	return foldYaw;
}

void tmgl_Init(int w, int h) {
	width = w; height = h;
	for (auto& s : stack) { s.clear(); Mat4 m; matIdentity(m.m); s.push_back(m); }
	resetCur();
	if (!prog) {
		prog = link(kVS, kFS);
		loc.P = glGetUniformLocation(prog, "uP");
		loc.E = glGetUniformLocation(prog, "uE");
		loc.MV = glGetUniformLocation(prog, "uMV");
		loc.T0 = glGetUniformLocation(prog, "uT0");
		loc.T1 = glGetUniformLocation(prog, "uT1");
		loc.Tex0 = glGetUniformLocation(prog, "uTex0");
		loc.Tex1 = glGetUniformLocation(prog, "uTex1");
		loc.AlphaFunc = glGetUniformLocation(prog, "uAlphaFunc");
		loc.AlphaRef = glGetUniformLocation(prog, "uAlphaRef");
		loc.Fog = glGetUniformLocation(prog, "uFog");
		loc.FogParams = glGetUniformLocation(prog, "uFogParams");
		loc.FogColor = glGetUniformLocation(prog, "uFogColor");
		loc.Opaque = glGetUniformLocation(prog, "uOpaque");
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, col));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv0));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv1));

		hudProg = link(kHudVS, kHudFS);
		hudLoc.MVP = glGetUniformLocation(hudProg, "uMVP");
		hudLoc.T = glGetUniformLocation(hudProg, "uT");
		hudLoc.Opacity = glGetUniformLocation(hudProg, "uOpacity");
		static const float quad[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
		glGenVertexArrays(1, &hudVao);
		glGenBuffers(1, &hudVbo);
		glBindVertexArray(hudVao);
		glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
		glBindVertexArray(0);
	}
	resetFrame();
}

void tmgl_Phase(int phase) { cur.phase = (unsigned char)phase; }

void tmgl_EndFrame(void) {
	if (inBegin) { inBegin = false; primVerts.clear(); }
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STREAM_DRAW);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	if (xr.active && xr.nviews > 0) {
		ensureHudTarget();
		glBindFramebuffer(GL_FRAMEBUFFER, hudFbo);
		glDisable(GL_SCISSOR_TEST);
		glViewport(0, 0, hudW, hudH);
		glDepthMask(GL_TRUE);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		replay(REPLAY_HUD, nullptr, 1.0f, 1.0f, 0, 0, true);
		glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);

		glBindFramebuffer(GL_FRAMEBUFFER, xr.fbo);
		glEnable(GL_SCISSOR_TEST);
		for (int i = 0; i < xr.nviews && i < 2; i++) {
			XrView v = xr.views[i];
			if (foldYaw != 0.0f) {	// take the head yaw the game's camera was turned by back out (tmgl_FoldHeadYaw)
				float r[16];
				matRotate(r, foldPitch, 1, 0, 0); matMul(v.eye, v.eye, r);
				matRotate(r, -foldYaw, 0, 1, 0); matMul(v.eye, v.eye, r);
				matRotate(r, -foldPitch, 1, 0, 0); matMul(v.eye, v.eye, r);
			}
			glViewport(v.vp[0], v.vp[1], v.vp[2], v.vp[3]);
			glScissor(v.vp[0], v.vp[1], v.vp[2], v.vp[3]);
			glDepthMask(GL_TRUE);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			replay(REPLAY_WORLD, &v, 1.0f, 1.0f, 0, 0, false);
			if (xr.hudOpacity > 0.0f) drawHudPanel(v);
		}
		glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		replay(REPLAY_ALL, nullptr, 1.0f, 1.0f, 0, 0, false);
	}

	glBindVertexArray(0);
	if (!pendingDeletes.empty()) {
		glDeleteTextures((GLsizei)pendingDeletes.size(), pendingDeletes.data());
		pendingDeletes.clear();
	}
	resetFrame();
}

// --- immediate mode -----------------------------------------------------------------------------------------------

void tmgl_Begin(GLenum mode) { primMode = mode; primVerts.clear(); inBegin = true; }
void tmgl_End(void) { if (inBegin) finishPrimitive(); inBegin = false; primVerts.clear(); }

static inline unsigned char toByte(float f) { return (unsigned char)(f <= 0.0f ? 0 : f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f)); }
static unsigned char curCol[4] = {255, 255, 255, 255};

void tmgl_Vertex3f(GLfloat x, GLfloat y, GLfloat z) {
	Vertex v;
	v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
	memcpy(v.col, curCol, 4);
	v.uv0[0] = curUV[0][0]; v.uv0[1] = curUV[0][1];
	v.uv1[0] = curUV[1][0]; v.uv1[1] = curUV[1][1];
	primVerts.push_back(v);
}
void tmgl_Vertex3fv(const GLfloat* v) { tmgl_Vertex3f(v[0], v[1], v[2]); }
void tmgl_Vertex2f(GLfloat x, GLfloat y) { tmgl_Vertex3f(x, y, 0.0f); }
void tmgl_Vertex2fv(const GLfloat* v) { tmgl_Vertex3f(v[0], v[1], 0.0f); }
void tmgl_TexCoord2f(GLfloat s, GLfloat t) { curUV[0][0] = s; curUV[0][1] = t; }
void tmgl_TexCoord2fv(const GLfloat* v) { curUV[0][0] = v[0]; curUV[0][1] = v[1]; }
void tmgl_MultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t) {
	int u = unit == GL_TEXTURE1 ? 1 : 0;
	curUV[u][0] = s; curUV[u][1] = t;
}
void tmgl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
	curColor[0] = r; curColor[1] = g; curColor[2] = b; curColor[3] = a;
	curCol[0] = toByte(r); curCol[1] = toByte(g); curCol[2] = toByte(b); curCol[3] = toByte(a);
}
void tmgl_Color3f(GLfloat r, GLfloat g, GLfloat b) { tmgl_Color4f(r, g, b, 1.0f); }
void tmgl_Color4fv(const GLfloat* v) { tmgl_Color4f(v[0], v[1], v[2], v[3]); }
void tmgl_Normal3f(GLfloat, GLfloat, GLfloat) {}

// --- state ----------------------------------------------------------------------------------------------------------

static void setCap(GLenum cap, unsigned char on) {
	switch (cap) {
	case GL_TEXTURE_2D: cur.tex2d[activeUnit] = on; break;
	case GL_BLEND: cur.blend = on; break;
	case GL_DEPTH_TEST: cur.depthTest = on; break;
	case GL_CULL_FACE: cur.cull = on; break;
	case K_FOG: cur.fog = on; break;
	case K_ALPHA_TEST: cur.alphaTest = on; break;
	case GL_POLYGON_OFFSET_FILL: cur.polyOffset = on; break;
	default: break;	// lighting etc.: not used by the game's drawing
	}
}
void tmgl_Enable(GLenum cap) { setCap(cap, 1); }
void tmgl_Disable(GLenum cap) { setCap(cap, 0); }

void tmgl_GetBooleanv(GLenum pname, GLboolean* params) {
	switch (pname) {
	case GL_TEXTURE_2D: *params = cur.tex2d[activeUnit]; break;
	case GL_BLEND: *params = cur.blend; break;
	case GL_DEPTH_TEST: *params = cur.depthTest; break;
	case GL_CULL_FACE: *params = cur.cull; break;
	case K_FOG: *params = cur.fog; break;
	case K_ALPHA_TEST: *params = cur.alphaTest; break;
	default: *params = 0;
	}
}
void tmgl_GetIntegerv(GLenum pname, GLint* params) {
	if (pname == GL_MAX_TEXTURE_SIZE) *params = 4096;
	else *params = 0;
}
const GLubyte* tmgl_GetString(GLenum name) {
	switch (name) {
	case GL_VENDOR: return (const GLubyte*)"treadmarks-vr";
	case GL_RENDERER: return (const GLubyte*)"WebGL 2 fixed-function layer";
	case GL_VERSION: return (const GLubyte*)"1.3";
	case GL_EXTENSIONS: return (const GLubyte*)"GL_ARB_multitexture GL_EXT_texture_edge_clamp";
	default: return (const GLubyte*)"";
	}
}
GLenum tmgl_GetError(void) { return 0; }

void tmgl_MatrixMode(GLenum mode) { matMode = mode; }
void tmgl_LoadIdentity(void) { matIdentity(top(matMode).m); markDirty(); }
void tmgl_LoadMatrixf(const GLfloat* m) { memcpy(top(matMode).m, m, 64); markDirty(); }
void tmgl_MultMatrixf(const GLfloat* m) { Mat4& t = top(matMode); matMul(t.m, t.m, m); markDirty(); }
void tmgl_PushMatrix(void) {
	int i = matMode == K_PROJECTION ? 1 : matMode == K_TEXTURE_MTX ? 2 : 0;
	stack[i].push_back(stack[i].back());
}
void tmgl_PopMatrix(void) {
	int i = matMode == K_PROJECTION ? 1 : matMode == K_TEXTURE_MTX ? 2 : 0;
	if (stack[i].size() > 1) stack[i].pop_back();
	markDirty();
}
void tmgl_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
	float len = sqrtf(x * x + y * y + z * z);
	if (len <= 0.0f) return;
	x /= len; y /= len; z /= len;
	float a = angle * 3.14159265358979f / 180.0f, c = cosf(a), s = sinf(a), ic = 1.0f - c;
	float r[16] = {
		x * x * ic + c, y * x * ic + z * s, x * z * ic - y * s, 0,
		x * y * ic - z * s, y * y * ic + c, y * z * ic + x * s, 0,
		x * z * ic + y * s, y * z * ic - x * s, z * z * ic + c, 0,
		0, 0, 0, 1};
	tmgl_MultMatrixf(r);
}
void tmgl_Translatef(GLfloat x, GLfloat y, GLfloat z) {
	float t[16];
	matIdentity(t);
	t[12] = x; t[13] = y; t[14] = z;
	tmgl_MultMatrixf(t);
}
void tmgl_Scalef(GLfloat x, GLfloat y, GLfloat z) {
	float t[16];
	matIdentity(t);
	t[0] = x; t[5] = y; t[10] = z;
	tmgl_MultMatrixf(t);
}
void tmgl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
	float m[16] = {0};
	m[0] = (float)(2 * n / (r - l));
	m[5] = (float)(2 * n / (t - b));
	m[8] = (float)((r + l) / (r - l));
	m[9] = (float)((t + b) / (t - b));
	m[10] = (float)(-(f + n) / (f - n));
	m[11] = -1.0f;
	m[14] = (float)(-2 * f * n / (f - n));
	tmgl_MultMatrixf(m);
}
void tmgl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
	float m[16];
	matIdentity(m);
	m[0] = (float)(2 / (r - l));
	m[5] = (float)(2 / (t - b));
	m[10] = (float)(-2 / (f - n));
	m[12] = (float)(-(r + l) / (r - l));
	m[13] = (float)(-(t + b) / (t - b));
	m[14] = (float)(-(f + n) / (f - n));
	tmgl_MultMatrixf(m);
}
void tmgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { cur.vp[0] = x; cur.vp[1] = y; cur.vp[2] = w; cur.vp[3] = h; }

void tmgl_Clear(GLbitfield mask) {
	Cmd c = {};
	c.type = CMD_CLEAR;
	c.clearMask = mask;
	memcpy(c.clearColor, clearColor, 16);
	c.state = stateIndex();
	cmds.push_back(c);
}
void tmgl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { clearColor[0] = r; clearColor[1] = g; clearColor[2] = b; clearColor[3] = a; }
void tmgl_DepthMask(GLboolean flag) { cur.depthMask = flag ? 1 : 0; }
void tmgl_DepthFunc(GLenum func) { cur.depthFunc = func; }
void tmgl_BlendFunc(GLenum s, GLenum d) { cur.blendSrc = s; cur.blendDst = d; }
void tmgl_AlphaFunc(GLenum func, GLclampf ref) { cur.alphaFunc = func; cur.alphaRef = ref; }
void tmgl_CullFace(GLenum mode) { cur.cullFace = mode; }
void tmgl_FrontFace(GLenum mode) { cur.frontFace = mode; }
void tmgl_PolygonMode(GLenum, GLenum) {}	// wireframe is a debug option; WebGL has no polygon mode
void tmgl_PolygonOffset(GLfloat factor, GLfloat units) { cur.polyFactor = factor; cur.polyUnits = units; }
void tmgl_ShadeModel(GLenum) {}
void tmgl_PointSize(GLfloat) {}

void tmgl_Fogf(GLenum pname, GLfloat param) {
	switch (pname) {
	case K_FOG_START: cur.fogStart = param; break;
	case K_FOG_END: cur.fogEnd = param; break;
	case K_FOG_DENSITY: cur.fogDensity = param; break;
	case K_FOG_MODE: cur.fogMode = (GLenum)param; break;
	}
}
void tmgl_Fogi(GLenum pname, GLint param) {
	if (pname == K_FOG_MODE) cur.fogMode = (GLenum)param;
	else tmgl_Fogf(pname, (GLfloat)param);
}
void tmgl_Fogfv(GLenum pname, const GLfloat* params) {
	if (pname == K_FOG_COLOR) memcpy(cur.fogColor, params, 16);
	else tmgl_Fogf(pname, params[0]);
}

void tmgl_TexEnvi(GLenum, GLenum pname, GLint param) {
	if (pname != K_TEXTURE_ENV_MODE) return;
	cur.env[activeUnit] = param == GL_REPLACE ? 1 : param == K_DECAL ? 2 : param == K_ADD ? 3 : 0;
}
void tmgl_TexEnvf(GLenum target, GLenum pname, GLfloat param) { tmgl_TexEnvi(target, pname, (GLint)param); }
void tmgl_ActiveTexture(GLenum texture) { activeUnit = texture == GL_TEXTURE1 ? 1 : 0; }

// --- textures -------------------------------------------------------------------------------------------------------
// Texture work happens straight away on texture unit 0; the replay rebinds whatever each draw needs.

static void bindForUpload() {
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, cur.tex[activeUnit]);
	applied = false;
}

void tmgl_GenTextures(GLsizei n, GLuint* ids) { glGenTextures(n, ids); }
void tmgl_DeleteTextures(GLsizei n, const GLuint* ids) {
	for (int i = 0; i < n; i++) {
		if (!ids[i]) continue;
		textures.erase(ids[i]);
		pendingDeletes.push_back(ids[i]);	// the frame being recorded may still draw with it
		for (int u = 0; u < 2; u++) if (cur.tex[u] == ids[i]) cur.tex[u] = 0;
	}
}
void tmgl_BindTexture(GLenum, GLuint texture) {
	cur.tex[activeUnit] = texture;
	if (texture && !textures.count(texture)) {
		// The game may bind ids it never got from glGenTextures (OpenGL 1 allows that; WebGL does not).
		TexInfo ti = {0, 0, -1, true};
		textures[texture] = ti;
	}
}
void tmgl_TexParameteri(GLenum, GLenum pname, GLint param) {
	if (!cur.tex[activeUnit]) return;
	if (param == K_CLAMP) param = GL_CLAMP_TO_EDGE;
	bindForUpload();
	glTexParameteri(GL_TEXTURE_2D, pname, param);
}
void tmgl_PixelStorei(GLenum pname, GLint param) {
	switch (pname) {
	case GL_UNPACK_ROW_LENGTH: unpackRowLength = param; break;
	case GL_UNPACK_SKIP_ROWS: unpackSkipRows = param; break;
	case GL_UNPACK_SKIP_PIXELS: unpackSkipPixels = param; break;
	case GL_UNPACK_ALIGNMENT: unpackAlignment = param; break;
	default: break;
	}
}

static bool formatHasAlpha(GLint internalformat) {
	switch (internalformat) {
	case 3: case GL_RGB: case K_R3_G3_B2: case K_RGB4: case K_RGB5: case K_RGB8: case K_LUMINANCE: return false;
	default: return true;
	}
}

// The game always passes RGBA bytes. Repack into a tight buffer (honouring the unpack settings) and force alpha to
// 255 for formats without alpha, as OpenGL 1 would.
static const void* prepPixels(const void* pixels, int w, int h, bool alpha) {
	if (!pixels) return nullptr;
	int rowLen = unpackRowLength > 0 ? unpackRowLength : w;
	if (alpha && rowLen == w && unpackSkipRows == 0 && unpackSkipPixels == 0) return pixels;
	scratch.resize((size_t)w * h * 4);
	const unsigned char* src = (const unsigned char*)pixels + ((size_t)unpackSkipRows * rowLen + unpackSkipPixels) * 4;
	for (int y = 0; y < h; y++) {
		memcpy(&scratch[(size_t)y * w * 4], src + (size_t)y * rowLen * 4, (size_t)w * 4);
		if (!alpha) for (int x = 0; x < w; x++) scratch[((size_t)y * w + x) * 4 + 3] = 255;
	}
	return scratch.data();
}

void tmgl_TexImage2D(GLenum, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint, GLenum format, GLenum type,
	const GLvoid* pixels) {
	GLuint id = cur.tex[activeUnit];
	if (!id) return;
	TexInfo& ti = textures[id];
	bool alpha = formatHasAlpha(internalformat);
	if (level == 0) { ti.w = w; ti.h = h; ti.alpha = alpha; ti.maxLevel = 0; }
	else if (level > ti.maxLevel) ti.maxLevel = level;
	bindForUpload();
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, prepPixels(pixels, w, h, alpha));
	// Only the levels the game uploads exist; tell WebGL so the texture stays complete.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ti.maxLevel);
}
void tmgl_TexSubImage2D(GLenum, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum, const GLvoid* pixels) {
	GLuint id = cur.tex[activeUnit];
	if (!id) return;
	bool alpha = textures.count(id) ? textures[id].alpha : true;
	bindForUpload();
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, prepPixels(pixels, w, h, alpha));
}
void tmgl_GetTexLevelParameteriv(GLenum, GLint level, GLenum pname, GLint* params) {
	auto it = textures.find(cur.tex[activeUnit]);
	int w = it != textures.end() ? it->second.w : 0, h = it != textures.end() ? it->second.h : 0;
	w = w >> level; h = h >> level;
	if (pname == K_TEXTURE_WIDTH) *params = w > 0 ? w : 1;
	else if (pname == K_TEXTURE_HEIGHT) *params = h > 0 ? h : 1;
	else *params = 0;
}
void tmgl_ReadPixels(GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum, GLvoid* pixels) {
	// Only used for the game's screenshot/animation capture, which the browser build doesn't offer.
	memset(pixels, 0, (size_t)w * h * 4);
}

} // extern "C"
