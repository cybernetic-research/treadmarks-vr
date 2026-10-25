// treadmarks-vr: included before every game source (tools/build.py, -include). Covers the few Windows calls the game
// makes outside #ifdef WIN32.
#ifndef TMVR_COMPAT_H
#define TMVR_COMPAT_H

#ifdef __cplusplus
static inline void SetActiveWindow(void*) {}	// TankGUI.cpp, before opening the web site

// Upstream declares this only for Qt builds but calls it in every non-headless build.
#include "Qt/Dedicated.h"
Dedicated* GetDedicatedWin();
#endif

#endif
