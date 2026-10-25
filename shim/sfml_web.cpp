// treadmarks-vr: browser implementation of the SFML stand-in (shim/SFML/*.hpp), plus the game's small OS hooks
// (save folders, opening a web link).
//
// Input arrives from the page (web/platform.js) through the exported tmvr_* functions below and is queued as SFML
// events, so the game's own event loop (CTankGame::DoSfmlEvents) handles keyboard, mouse and VR controllers alike.
// Sound and music are played by the page with Web Audio (EM_JS functions here).

#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <GL/glew.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "../game/CStr.h"
#include "../game/Directories.h"
#include "../game/Web.h"

// --- page side ------------------------------------------------------------------------------------------------------

EM_JS(void, tmvr_js_canvas_size, (int w, int h), { Module.tmvrSetCanvasSize && Module.tmvrSetCanvasSize(w, h); });
EM_JS(int, tmvr_js_desktop_w, (), { return Module.tmvrDesktopSize ? Module.tmvrDesktopSize()[0] : 1280; });
EM_JS(int, tmvr_js_desktop_h, (), { return Module.tmvrDesktopSize ? Module.tmvrDesktopSize()[1] : 960; });
EM_JS(void, tmvr_js_frame_done, (), { Module.tmvrFrameDone && Module.tmvrFrameDone(); });

EM_JS(int, tmvr_js_snd_create, (const unsigned char* data, int len), { return Module.tmvrAudio.create(HEAPU8.slice(data, data + len)); });
EM_JS(void, tmvr_js_snd_free, (int buf), { Module.tmvrAudio.free(buf); });
EM_JS(int, tmvr_js_voice_new, (), { return Module.tmvrAudio.voiceNew(); });
EM_JS(void, tmvr_js_voice_free, (int v), { Module.tmvrAudio.voiceFree(v); });
EM_JS(void, tmvr_js_voice_play, (int v, int buf, int loop, float vol, float pitch), { Module.tmvrAudio.play(v, buf, loop, vol, pitch); });
EM_JS(void, tmvr_js_voice_stop, (int v), { Module.tmvrAudio.stop(v); });
EM_JS(int, tmvr_js_voice_playing, (int v), { return Module.tmvrAudio.playing(v); });
EM_JS(void, tmvr_js_voice_set, (int v, float vol, float pitch), { Module.tmvrAudio.set(v, vol, pitch); });
EM_JS(void, tmvr_js_global_volume, (float vol), { Module.tmvrAudio.globalVolume(vol); });
EM_JS(void, tmvr_js_music_open, (const char* path), { Module.tmvrAudio.musicOpen(UTF8ToString(path)); });
EM_JS(void, tmvr_js_music_cmd, (int cmd), { Module.tmvrAudio.musicCmd(cmd); });
EM_JS(int, tmvr_js_music_status, (), { return Module.tmvrAudio.musicStatus(); });
EM_JS(void, tmvr_js_music_volume, (float vol), { Module.tmvrAudio.musicVolume(vol); });
EM_JS(void, tmvr_js_open_link, (const char* url), { window.open(UTF8ToString(url), '_blank'); });

// --- window and events ----------------------------------------------------------------------------------------------

namespace {
std::deque<sf::Event> events;
bool keyDown[sf::Keyboard::KeyCount];
bool mouseCaptured = false;
int canvasW = 0, canvasH = 0;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE glContext = 0;

struct Joy {
	int connected, buttons, axes;	// axes: bit mask of sf::Joystick::Axis present
	float axis[8];
	int pressed[32];
};
Joy joys[sf::Joystick::Count];
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void tmvr_key(int code, int down, int alt, int ctrl, int shift) {
	if (code < 0 || code >= sf::Keyboard::KeyCount) return;
	if (down && keyDown[code]) return;	// the game counts presses itself; no key repeat
	keyDown[code] = down != 0;
	sf::Event e;
	e.type = down ? sf::Event::KeyPressed : sf::Event::KeyReleased;
	e.key.code = (sf::Keyboard::Key)code;
	e.key.alt = alt; e.key.control = ctrl; e.key.shift = shift; e.key.system = false;
	events.push_back(e);
}
EMSCRIPTEN_KEEPALIVE void tmvr_text(int unicode) {
	sf::Event e;
	e.type = sf::Event::TextEntered;
	e.text.unicode = unicode;
	events.push_back(e);
}
EMSCRIPTEN_KEEPALIVE void tmvr_mouse_move(int x, int y) {
	sf::Event e;
	e.type = sf::Event::MouseMoved;
	e.mouseMove.x = x; e.mouseMove.y = y;
	events.push_back(e);
}
EMSCRIPTEN_KEEPALIVE void tmvr_mouse_button(int button, int down, int x, int y) {
	sf::Event e;
	e.type = down ? sf::Event::MouseButtonPressed : sf::Event::MouseButtonReleased;
	e.mouseButton.button = (sf::Mouse::Button)button;
	e.mouseButton.x = x; e.mouseButton.y = y;
	events.push_back(e);
}
EMSCRIPTEN_KEEPALIVE void tmvr_focus(int focused) {
	sf::Event e;
	e.type = focused ? sf::Event::GainedFocus : sf::Event::LostFocus;
	events.push_back(e);
	if (!focused) {
		for (int k = 0; k < sf::Keyboard::KeyCount; k++) if (keyDown[k]) tmvr_key(k, 0, 0, 0, 0);
	}
}
// The game recentres the mouse every frame while driving (mouse look). The page asks so it can report relative motion.
EMSCRIPTEN_KEEPALIVE int tmvr_mouse_captured() { int c = mouseCaptured; mouseCaptured = false; return c; }

EMSCRIPTEN_KEEPALIVE void tmvr_joy(int index, int connected, int buttons, int axes) {
	if (index < 0 || index >= sf::Joystick::Count) return;
	joys[index].connected = connected; joys[index].buttons = buttons; joys[index].axes = axes;
}
EMSCRIPTEN_KEEPALIVE void tmvr_joy_axis(int index, int axis, float value) {
	if (index >= 0 && index < sf::Joystick::Count && axis >= 0 && axis < 8) joys[index].axis[axis] = value;
}
EMSCRIPTEN_KEEPALIVE void tmvr_joy_button(int index, int button, int down) {
	if (index >= 0 && index < sf::Joystick::Count && button >= 0 && button < 32) joys[index].pressed[button] = down;
}

}

namespace sf {

VideoMode VideoMode::getDesktopMode() { return VideoMode(tmvr_js_desktop_w(), tmvr_js_desktop_h(), 32); }

const std::vector<VideoMode>& VideoMode::getFullscreenModes() {
	static std::vector<VideoMode> modes;
	if (modes.empty()) {
		static const int sizes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 960}, {1600, 900},
			{1600, 1200}, {1920, 1080}};
		for (auto& s : sizes) modes.push_back(VideoMode(s[0], s[1], 32));
	}
	return modes;
}

bool Keyboard::isKeyPressed(Key key) { return key >= 0 && key < KeyCount && keyDown[key]; }

void Mouse::setPosition(const Vector2i&) { mouseCaptured = true; }

bool Joystick::isConnected(unsigned int j) { return j < Count && joys[j].connected; }
unsigned int Joystick::getButtonCount(unsigned int j) { return j < Count ? joys[j].buttons : 0; }
bool Joystick::hasAxis(unsigned int j, Axis a) { return j < Count && (joys[j].axes >> a) & 1; }
bool Joystick::isButtonPressed(unsigned int j, unsigned int b) { return j < Count && b < 32 && joys[j].pressed[b]; }
float Joystick::getAxisPosition(unsigned int j, Axis a) { return j < Count && a < 8 ? joys[j].axis[a] * 100.0f : 0.0f; }

void Window::create(VideoMode mode, const std::string&, unsigned int, const ContextSettings&) {
	canvasW = mode.width; canvasH = mode.height;
	tmvr_js_canvas_size(canvasW, canvasH);
	if (!glContext) {
		EmscriptenWebGLContextAttributes attr;
		emscripten_webgl_init_context_attributes(&attr);
		attr.majorVersion = 2;
		attr.minorVersion = 0;
		attr.alpha = false;
		attr.depth = true;
		attr.antialias = true;
		attr.premultipliedAlpha = false;
		attr.preserveDrawingBuffer = false;
		glContext = emscripten_webgl_create_context("#canvas", &attr);
		if (glContext <= 0) printf("treadmarks-vr: WebGL 2 is not available (%d)\n", (int)glContext);
		emscripten_webgl_make_context_current(glContext);
	}
	tmgl_Init(canvasW, canvasH);
	open = true;
}

void Window::setSize(const Vector2u& size) {
	canvasW = size.x; canvasH = size.y;
	tmvr_js_canvas_size(canvasW, canvasH);
	tmgl_Init(canvasW, canvasH);
}

bool Window::pollEvent(Event& event) {
	if (events.empty()) return false;
	event = events.front();
	events.pop_front();
	return true;
}

void Window::display() {
	tmgl_EndFrame();
	tmvr_js_frame_done();
}

// --- audio ----------------------------------------------------------------------------------------------------------

SoundBuffer::SoundBuffer() : bufferId(0) {}
SoundBuffer::~SoundBuffer() { if (bufferId) tmvr_js_snd_free(bufferId); }

bool SoundBuffer::loadFromFile(const std::string& filename) {
	FILE* f = fopen(filename.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<unsigned char> data(len > 0 ? len : 0);
	bool ok = len > 0 && fread(data.data(), 1, len, f) == (size_t)len;
	fclose(f);
	if (!ok) return false;
	if (bufferId) tmvr_js_snd_free(bufferId);
	bufferId = tmvr_js_snd_create(data.data(), (int)len);
	return bufferId != 0;
}

Sound::Sound() : voiceId(tmvr_js_voice_new()), bufferId(0), bLoop(false), fVolume(100.0f), fPitch(1.0f) {}
Sound::Sound(const Sound& o) : voiceId(tmvr_js_voice_new()), bufferId(o.bufferId), bLoop(o.bLoop), fVolume(o.fVolume), fPitch(o.fPitch) {}
Sound& Sound::operator=(const Sound& o) {
	bufferId = o.bufferId; bLoop = o.bLoop; fVolume = o.fVolume; fPitch = o.fPitch;
	return *this;
}
Sound::~Sound() { tmvr_js_voice_free(voiceId); }
void Sound::setBuffer(const SoundBuffer& buffer) { stop(); bufferId = buffer.id(); }
void Sound::play() { tmvr_js_voice_play(voiceId, bufferId, bLoop, fVolume / 100.0f, fPitch); }
void Sound::stop() { tmvr_js_voice_stop(voiceId); }
SoundSource::Status Sound::getStatus() const { return tmvr_js_voice_playing(voiceId) ? Playing : Stopped; }
void Sound::setVolume(float volume) { fVolume = volume; tmvr_js_voice_set(voiceId, fVolume / 100.0f, fPitch); }
void Sound::setPitch(float pitch) { fPitch = pitch; tmvr_js_voice_set(voiceId, fVolume / 100.0f, fPitch); }

bool Music::openFromFile(const std::string& filename) { tmvr_js_music_open(filename.c_str()); return true; }
void Music::play() { tmvr_js_music_cmd(1); }
void Music::pause() { tmvr_js_music_cmd(2); }
void Music::stop() { tmvr_js_music_cmd(0); }
SoundSource::Status Music::getStatus() const { return (Status)tmvr_js_music_status(); }
void Music::setVolume(float volume) { tmvr_js_music_volume(volume / 100.0f); }

void Listener::setGlobalVolume(float volume) { tmvr_js_global_volume(volume / 100.0f); }

} // namespace sf

// --- OS hooks (game/Directories.h, game/Web.h) ----------------------------------------------------------------------
// Saved settings, scores and ladders live in /save, which the page keeps in IndexedDB.

CStr GetAppDataDir() { return CStr("/save/user/"); }
CStr GetCommonAppDataDir() { return CStr("/save/common/"); }
void OpenWebLink(const char* link) { tmvr_js_open_link(link); }
