# Tread Marks VR

[Tread Marks](https://github.com/LongbowGames/TreadMarks) (Longbow Digital Arts, 2000), the tank racing and combat
game, running in a web browser and in VR headsets through WebXR.

This is a port, not a remake: the game's own code (physics, AI, maps, menus, chase camera) is compiled to WebAssembly
unchanged apart from a few dozen marked lines. Everything new is the layer underneath that a browser and a headset need.

- Plays in desktop browsers with keyboard and mouse, or a gamepad.
- In a VR headset (tested on Meta Quest): the 3D world is drawn in stereo around you from the original chase camera,
  with head tracking. Menus and the HUD appear on a large screen in front of you that you point at and click.

## How it works

| Part | What it does |
| --- | --- |
| `game/` | The original game source (`src/game` from LongbowGames/TreadMarks at `6af9528`, without the Qt dedicated-server window). The first commit is the unmodified source, so `git diff 97ffe6a -- game` shows every change; each one is marked `treadmarks-vr:`. |
| `shim/GL/`, `shim/gl1.cpp` | OpenGL 1.x fixed-function pipeline (immediate mode, matrix stack, fog, alpha test, multitexture) on WebGL 2. Each frame is recorded once and replayed: in order on a flat screen, or once per eye in VR with the 2D layer rendered to a texture for the in-world screen. |
| `shim/SFML/`, `shim/sfml_web.cpp` | Stand-ins for the parts of SFML the game uses (window, events, keyboard, mouse, joystick, sound, music), backed by the page. |
| `web/platform.js` | Loads the game data into the in-memory filesystem (cached in the browser), makes that filesystem behave like Windows (case-insensitive, backslash paths), feeds input, plays sound with Web Audio, keeps settings and ladders in IndexedDB. |
| `web/xr.js` | WebXR session, per-eye matrices, the in-world screen, and Touch controller input. |
| `tools/build.py` | Builds with Emscripten (no CMake needed). |
| `tools/serve.mjs` | Small local server for the page and the game data (gzip, range requests, optional HTTPS for headsets). |

Two VR details worth knowing:

- The game only prepares (culls) terrain for its own narrow view. In VR it is asked for a wider angle, and the world is
  drawn with the game's camera turned to where your head is looking, with that turn taken back out of the eye matrices,
  so the picture is identical but nothing you turn to look at is missing.
- 2D drawing (menus, HUD, countdown) goes to the in-world screen; only the 3D world is drawn per eye.

## Building

You need [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk, tested with 6.0.12),
Python 3 and Node.js.

```sh
python tools/build.py          # optimised build -> web/tm.js, web/tm.wasm
python tools/build.py --debug  # unoptimised, with assertions
```

The build looks for emsdk in `$EMSDK` or `~/emsdk`. Object files go to `%LOCALAPPDATA%/treadmarks-vr-build` (or
`~/.cache/treadmarks-vr-build`), or wherever `TMVR_OBJ` points.

## Game data

The game's art, maps, models and sounds are not in this repository. They are in the original game's repository
(also GPLv3), in its `Resources/` folder (about 330 MB):

```sh
git clone https://github.com/LongbowGames/TreadMarks.git ../treadmarks-vr-upstream
```

The server looks for the data in `../treadmarks-vr-upstream/Resources` by default, or use `--data <folder>`.

## Running

```sh
node tools/serve.mjs            # http://localhost:8097
node tools/serve.mjs --https    # https://<this computer's address>:8097, for a headset on the same network
```

Open the page, wait for the data to load (about 99 MB transferred the first time; after that it is cached by the
browser on HTTPS or localhost), and press Play.

For a headset: WebXR needs a secure page, so either use `--https` and accept the self-signed certificate warning once
(the certificate is made with `openssl` on first use and kept in `tools/.cert`), or connect a Quest by USB, run
`adb reverse tcp:8097 tcp:8097` and open `http://localhost:8097` in the headset browser. Then press **Enter VR**.

For static hosting, copy `web/` and the data folder (as `web/data/`), and write the data list with
`node tools/serve.mjs --manifest web/data/manifest.json`.

## Controls

Keyboard and mouse are as in the original game (arrow keys or WASD to drive, Z/C or the mouse to turn the gun, Space
or left mouse button to fire, Escape for the menu). Controllers are mapped onto those default keys, so nothing needs
setting up in the game's options:

| | Gamepad | VR controllers |
| --- | --- | --- |
| Drive | Left stick or d-pad | Left stick |
| Turn the gun | Right stick | Right stick left/right |
| Fire | Right trigger or right bumper; A while racing | Either trigger |
| Select in menus | A | Point the right controller at the screen and pull the trigger, or A |
| Back / menu | B or Start | B |
| Scores | Left trigger | X |
| Look back | Y | Y |
| Reset camera | Left bumper | Left stick click |
| Tilt camera | | Right stick up/down |
| Turret camera | | Right grip |

## Debugging aids

- `window.tmvrStats.frameMs` in the browser console: average time the game takes per frame.
- `?fakexr` on the page address (or `window.tmvrFakeXr = true`): draws the VR path on a flat screen as two side-by-side
  eyes, and `window.tmvrFakeYaw = 60` simulates turning your head. Useful without a headset.

## Licence and credits

Tread Marks is © Longbow Digital Arts and was released under the
[GNU General Public License v3](LICENSE) by Longbow Games, with its game data. This port is released under the same
licence. Thanks to Longbow Games for releasing the game.
