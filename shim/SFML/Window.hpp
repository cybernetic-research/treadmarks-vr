// treadmarks-vr: a stand-in for the parts of SFML 2.4 (window, events, keyboard, mouse, joystick) that Tread Marks
// uses, implemented on the browser (shim/sfml_web.cpp). Names and enum order follow SFML so the game code is unchanged.
#ifndef TMVR_SFML_WINDOW_HPP
#define TMVR_SFML_WINDOW_HPP

#include <string>
#include <vector>

namespace sf {

template <typename T> struct Vector2 {
	T x, y;
	Vector2() : x(0), y(0) {}
	Vector2(T X, T Y) : x(X), y(Y) {}
};
typedef Vector2<int> Vector2i;
typedef Vector2<unsigned int> Vector2u;
typedef Vector2<float> Vector2f;

struct ContextSettings {
	unsigned int depthBits, stencilBits, antialiasingLevel, majorVersion, minorVersion;
	explicit ContextSettings(unsigned int depth = 0, unsigned int stencil = 0, unsigned int antialiasing = 0,
		unsigned int major = 1, unsigned int minor = 1)
		: depthBits(depth), stencilBits(stencil), antialiasingLevel(antialiasing), majorVersion(major), minorVersion(minor) {}
};

class VideoMode {
public:
	unsigned int width, height, bitsPerPixel;
	VideoMode() : width(0), height(0), bitsPerPixel(32) {}
	VideoMode(unsigned int w, unsigned int h, unsigned int bpp = 32) : width(w), height(h), bitsPerPixel(bpp) {}
	static VideoMode getDesktopMode();
	static const std::vector<VideoMode>& getFullscreenModes();
	bool isValid() const { return width > 0 && height > 0; }
};

namespace Style { enum { None = 0, Titlebar = 1, Resize = 2, Close = 4, Fullscreen = 8, Default = 7 }; }

class Keyboard {
public:
	enum Key {
		Unknown = -1,
		A = 0, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		Escape, LControl, LShift, LAlt, LSystem, RControl, RShift, RAlt, RSystem, Menu,
		LBracket, RBracket, SemiColon, Comma, Period, Quote, Slash, BackSlash, Tilde, Equal, Dash,
		Space, Return, BackSpace, Tab, PageUp, PageDown, End, Home, Insert, Delete,
		Add, Subtract, Multiply, Divide, Left, Right, Up, Down,
		Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
		Pause,
		KeyCount
	};
	static bool isKeyPressed(Key key);
};

class Mouse {
public:
	enum Button { Left, Right, Middle, XButton1, XButton2, ButtonCount };
	static void setPosition(const Vector2i& position);
	template <typename W> static void setPosition(const Vector2i& position, const W&) { setPosition(position); }
};

class Joystick {
public:
	enum { Count = 8, ButtonCount = 32, AxisCount = 8 };
	enum Axis { X, Y, Z, R, U, V, PovX, PovY };
	static bool isConnected(unsigned int joystick);
	static unsigned int getButtonCount(unsigned int joystick);
	static bool hasAxis(unsigned int joystick, Axis axis);
	static bool isButtonPressed(unsigned int joystick, unsigned int button);
	static float getAxisPosition(unsigned int joystick, Axis axis);
};

class Event {
public:
	struct SizeEvent { unsigned int width, height; };
	struct KeyEvent { Keyboard::Key code; bool alt, control, shift, system; };
	struct TextEvent { unsigned int unicode; };
	struct MouseMoveEvent { int x, y; };
	struct MouseButtonEvent { Mouse::Button button; int x, y; };
	enum EventType {
		Closed, Resized, LostFocus, GainedFocus, TextEntered, KeyPressed, KeyReleased,
		MouseWheelMoved, MouseWheelScrolled, MouseButtonPressed, MouseButtonReleased, MouseMoved,
		MouseEntered, MouseLeft, Count
	};
	EventType type;
	union {
		SizeEvent size;
		KeyEvent key;
		TextEvent text;
		MouseMoveEvent mouseMove;
		MouseButtonEvent mouseButton;
	};
};

class Window {
public:
	Window() : open(false) {}
	void create(VideoMode mode, const std::string& title, unsigned int style = Style::Default,
		const ContextSettings& settings = ContextSettings());
	void close() { open = false; }
	bool isOpen() const { return open; }
	bool setActive(bool active = true) { return true; }
	void setIcon(unsigned int, unsigned int, const unsigned char*) {}
	void setMouseCursorVisible(bool) {}
	void setSize(const Vector2u& size);
	bool pollEvent(Event& event);
	void display();
private:
	bool open;
};

} // namespace sf

#endif
