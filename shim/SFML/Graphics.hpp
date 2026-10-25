// treadmarks-vr: stand-in for sf::Image, the only part of SFML Graphics the game uses (to set the window icon).
#ifndef TMVR_SFML_GRAPHICS_HPP
#define TMVR_SFML_GRAPHICS_HPP

#include "Window.hpp"

namespace sf {

class Image {
public:
	bool loadFromFile(const std::string&) { return false; }
	Vector2u getSize() const { return Vector2u(0, 0); }
	const unsigned char* getPixelsPtr() const { return nullptr; }
};

} // namespace sf

#endif
