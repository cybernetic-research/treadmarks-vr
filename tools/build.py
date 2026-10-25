#!/usr/bin/env python3
# treadmarks-vr: builds the game to WebAssembly (web/tm.js + web/tm.wasm) with Emscripten.
#
#   python tools/build.py            optimised build
#   python tools/build.py --debug    -O0, assertions and source names (slow, for crash hunting)
#   python tools/build.py --clean    throw away the object files first
#
# Object files go to a local folder (TMVR_OBJ, default %LOCALAPPDATA%/treadmarks-vr-build) rather than the repo,
# which may be on a network share. Only sources newer than their object (or whose headers changed) are recompiled.
# Jobs are capped (TMVR_JOBS, default 6) to keep the machine responsive.

import os, subprocess, sys, shutil, concurrent.futures

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'game')
SHIM = os.path.join(ROOT, 'shim')
WEB = os.path.join(ROOT, 'web')

# Same list as game/CMakeLists.txt for the GUI build, minus Qt and the per-OS files (shim/sfml_web.cpp has the OS hooks).
GAME_SRCS = '''
CJoyInput.cpp GenericBuffer.cpp Poly.cpp Render.cpp SimpleStatus.cpp Sound.cpp TankGUI.cpp
BanList.cpp Basis.cpp BitPacking.cpp CamRbd.cpp CfgParse.cpp Compression.cpp Crypt.cpp CStr.cpp
EntityBase.cpp EntityFlag.cpp EntityGUI.cpp EntityPFire.cpp EntityRacetank.cpp EntityTank.cpp
FileCRCList.cpp FileManager.cpp Find.cpp Heartbeat.cpp HelpText.cpp IFF.cpp Image.cpp LadderManager.cpp
Mesh.cpp Networking.cpp neuquant.c PacketProcessors.cpp Physics.cpp pi.cpp Quantizer.cpp Reg.cpp
ResourceManager.cpp StatsPackage.cpp TankAI.cpp TankGame.cpp TankRacing.cpp Terrain.cpp TextLine.cpp
Timer.cpp TxtBlock.cpp vecmath.cpp Vector.cpp VoxelWorld.cpp
GL/GLPolyRender.cpp GL/GLTerrain.cpp GL/GLTerrainRender.cpp GL/GLTerrainRender2.cpp GL/GLTerrainRender3.cpp
'''.split()
SHIM_SRCS = ['gl1.cpp', 'sfml_web.cpp']

EXPORTS = ['_main', '_tmvr_frame', '_tmvr_key', '_tmvr_text', '_tmvr_mouse_move', '_tmvr_mouse_button', '_tmvr_focus',
	'_tmvr_mouse_captured', '_tmvr_joy', '_tmvr_joy_axis', '_tmvr_joy_button', '_tmgl_XrFrame', '_tmgl_Width',
	'_tmgl_Height', '_malloc', '_free']


def emsdk_tool(name):
	for base in [os.environ.get('EMSDK', ''), os.path.expanduser('~/emsdk')]:
		if not base:
			continue
		for ext in ['.exe', '.bat', '']:
			p = os.path.join(base, 'upstream', 'emscripten', name + ext)
			if os.path.exists(p):
				return p
	found = shutil.which(name)
	if not found:
		sys.exit('treadmarks-vr: cannot find %s; install emsdk to ~/emsdk or set EMSDK' % name)
	return found


def main():
	debug = '--debug' in sys.argv
	obj_root = os.environ.get('TMVR_OBJ') or os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~/.cache')),
		'treadmarks-vr-build')
	obj_dir = os.path.join(obj_root, 'debug' if debug else 'release')
	if '--clean' in sys.argv and os.path.isdir(obj_dir):
		shutil.rmtree(obj_dir)
	os.makedirs(obj_dir, exist_ok=True)
	jobs = int(os.environ.get('TMVR_JOBS', '6'))
	emcc, empp = emsdk_tool('emcc'), emsdk_tool('em++')

	opt = ['-O0', '-g'] if debug else ['-O2']
	common = opt + ['-I' + SHIM, '-I' + GAME, '-DTMVR=1', '-sUSE_ZLIB=1',
		'-Wno-everything']	# 1990s code: thousands of harmless warnings would hide the real errors
	cxx = common + ['-std=c++17', '-fexceptions']

	units = [(os.path.join(GAME, s), s) for s in GAME_SRCS] + [(os.path.join(SHIM, s), 'shim/' + s) for s in SHIM_SRCS]
	todo = []
	objs = []
	for src, name in units:
		obj = os.path.join(obj_dir, name.replace('/', '_') + '.o')
		dep = obj[:-2] + '.d'
		objs.append(obj)
		if needs_build(src, obj, dep):
			todo.append((src, obj, dep, name))

	def compile_one(item):
		src, obj, dep, name = item
		tool = emcc if src.endswith('.c') else empp
		flags = common if src.endswith('.c') else cxx
		r = subprocess.run([tool] + flags + ['-MMD', '-MF', dep, '-c', src, '-o', obj], capture_output=True, text=True)
		return name, r.returncode, (r.stdout + r.stderr).strip()

	failed = []
	print('treadmarks-vr: compiling %d of %d files (%s)' % (len(todo), len(units), 'debug' if debug else 'release'))
	with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
		for name, code, out in pool.map(compile_one, todo):
			if code != 0:
				failed.append(name)
				print('--- %s FAILED\n%s' % (name, out))
			else:
				print('  ' + name)
	if failed:
		sys.exit('treadmarks-vr: %d file(s) failed: %s' % (len(failed), ' '.join(failed)))

	link = [empp] + opt + objs + ['-sUSE_ZLIB=1', '-fexceptions',
		'-sMAX_WEBGL_VERSION=2', '-sMIN_WEBGL_VERSION=2',
		'-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=512MB', '-sMAXIMUM_MEMORY=4GB', '-sSTACK_SIZE=4MB',
		'-sFORCE_FILESYSTEM=1', '-sEXIT_RUNTIME=0', '-sMODULARIZE=1', '-sEXPORT_NAME=TreadMarks',
		'-sINVOKE_RUN=0', '-sENVIRONMENT=web',
		'-sEXPORTED_FUNCTIONS=' + ','.join(EXPORTS),
		'-sEXPORTED_RUNTIME_METHODS=FS,callMain,HEAPU8,HEAPF32,HEAP32,ccall',
		'-o', os.path.join(WEB, 'tm.js')]
	if debug:
		link += ['-sASSERTIONS=2', '-sSAFE_HEAP=0']
	print('treadmarks-vr: linking')
	r = subprocess.run(link, capture_output=True, text=True)
	out = (r.stdout + r.stderr).strip()
	if out:
		print(out)
	if r.returncode != 0:
		sys.exit('treadmarks-vr: link failed')
	print('treadmarks-vr: built web/tm.js and web/tm.wasm')


def needs_build(src, obj, dep):
	if not os.path.exists(obj) or not os.path.exists(dep):
		return True
	t = os.path.getmtime(obj)
	try:
		text = open(dep, encoding='utf-8', errors='replace').read().replace('\\\n', ' ')
	except OSError:
		return True
	# "obj: src hdr1 hdr2 ..." with backslash-escaped spaces
	deps = text.split(': ', 1)[1] if ': ' in text else ''	# not ':' alone: Windows drive letters
	deps = deps.replace('\\ ', '\0').split()
	for d in [src] + [x.replace('\0', ' ') for x in deps]:
		if os.path.exists(d) and os.path.getmtime(d) > t:
			return True
	return False


if __name__ == '__main__':
	main()
