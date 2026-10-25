// treadmarks-vr: stand-in for the Qt dedicated-server window (game/Qt, not built). The browser build is never a
// dedicated server, but the game still references the window, so console output goes to the browser console.
#ifndef __DEDICATED_H__
#define __DEDICATED_H__

#include <cstdio>

class Dedicated
{
public:
	void Reinit() {}
	void OutputFunc(const char *text, int) { printf("%s", text); }
	void show() {}
};

#endif
