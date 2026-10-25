// treadmarks-vr: stand-in for the parts of SFML Audio the game uses. Sounds are decoded and played with Web Audio
// (web/platform.js); music streams from the server through an <audio> element instead of being loaded into memory.
// The game does its own 3D attenuation (Sound.cpp sets attenuation 0 and computes the volume), so positions are ignored.
#ifndef TMVR_SFML_AUDIO_HPP
#define TMVR_SFML_AUDIO_HPP

#include <string>

namespace sf {

class SoundSource {
public:
	enum Status { Stopped, Paused, Playing };
};

class SoundBuffer {
public:
	SoundBuffer();
	~SoundBuffer();
	bool loadFromFile(const std::string& filename);
	int id() const { return bufferId; }
private:
	SoundBuffer(const SoundBuffer&);
	SoundBuffer& operator=(const SoundBuffer&);
	int bufferId;
};

class Sound : public SoundSource {
public:
	Sound();
	Sound(const Sound& other);
	Sound& operator=(const Sound& other);
	~Sound();
	void setBuffer(const SoundBuffer& buffer);
	void play();
	void pause() { stop(); }
	void stop();
	Status getStatus() const;
	void setLoop(bool loop) { bLoop = loop; }
	void setVolume(float volume);
	void setPitch(float pitch);
	void setPosition(float, float, float) {}
	void setAttenuation(float) {}
	void setRelativeToListener(bool) {}
	void setMinDistance(float) {}
private:
	int voiceId;
	int bufferId;
	bool bLoop;
	float fVolume, fPitch;
};

class Music : public SoundSource {
public:
	bool openFromFile(const std::string& filename);
	void play();
	void pause();
	void stop();
	Status getStatus() const;
	void setVolume(float volume);
	void setLoop(bool) {}
};

class Listener {
public:
	static void setGlobalVolume(float volume);
	static void setPosition(float, float, float) {}
	static void setDirection(float, float, float) {}
};

} // namespace sf

#endif
