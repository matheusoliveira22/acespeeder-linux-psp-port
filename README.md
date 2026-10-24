# AceSpeeder! for PSP, Linux and Android

A source port of **AceSpeeder!**, the freeware Windows racing game by Takahiro
Nakatani / Production TN (2000), to the Sony PSP and Android, with a Linux
build used for development and testing.

The port is a reimplementation, not an emulator: the original executable was
decompiled with Ghidra and its game logic rewritten in portable C. The race
logic is checked against the original, which runs under an x86 emulator next to
the port, and matches it bit for bit (the shipped replays finish with the same
times). Graphics, sound and input go through a small platform layer: SDL2 and
OpenGL on Linux, the PSP SDK's sceGu on the PSP, SDL3 and OpenGL ES 3 on
Android (and macOS).

**The original game files are not part of this repository.** You need your own
copy of the AceSpeeder! folder. The port reads its meshes, bitmaps, sounds and
replays, and converts a few things from it at build time. AceSpeeder! is the
work of its author; keep the port and the converted files for personal use.

## Contents

- [Repository layout](#repository-layout)
- [Requirements](#requirements)
- [Building](#building)
- [Where the outputs go](#where-the-outputs-go)
- [Putting the game on a PSP](#putting-the-game-on-a-psp)
- [Playing](#playing)
- [Development](#development)

## Repository layout

```
AceSpeeder/
├── game/                   the original game folder (not in git; you provide it)
├── port/                   the port
│   ├── CMakeLists.txt      Linux build (and the sdl3 backend's native code)
│   ├── Makefile.psp        PSP build
│   ├── android/            Android app (Gradle), see android/README.md
│   ├── src/
│   │   ├── main.c          entry point and main loop (MainFrame: 33 ms logic ticks)
│   │   ├── game/           the game, reimplemented from the executable
│   │   ├── engine/         Direct3D Retained Mode stand-in, .x/.bmp loaders, font, sprites
│   │   ├── platform/       platform layer: platform.h, mixer, software clipping
│   │   │   ├── linux/      SDL2 + OpenGL backend
│   │   │   ├── psp/        sceGu backend
│   │   │   └── sdl3/       SDL3 + OpenGL ES 3 backend (Android, macOS)
│   │   ├── third_party/    stb_vorbis
│   │   └── tools/          viewer, trackview (debug programs), testlib.c (for the tests),
│   │                       replaycheck (plays the shipped replays without drawing)
│   ├── third_party/SDL/    SDL3 (git submodule, for the sdl3 backend)
│   ├── tools/              build-time tools (data conversion, icons, packaging)
│   ├── data/               generated data (not in git)
│   ├── build/              Linux build directory (not in git)
│   └── dist/               packaged PSP game (not in git)
└── re/                     reverse engineering
    ├── NOTES.md            findings: structures, file formats, game states, quirks
    ├── names.txt           names given to the executable's functions and globals
    ├── decomp/             annotated decompilation, made by run.sh (not in git)
    ├── scripts/            Ghidra scripts, emulator, data extractors, differential tests
    └── run.sh              rebuilds the Ghidra project and decomp/ from scratch
```

### The game code (`port/src/game/`)

Each file corresponds to a part of the original; the comments name the
original functions by address (e.g. `Race_Update (0x411530)`), so the code can
be compared with the decompilation that `re/run.sh` writes to `re/decomp/`.

| File | What it is |
|---|---|
| `gmath.c` | vectors, matrices, track positions; helpers that reproduce the x87's single-precision rounding |
| `track.c` | courses: loading `track_*.bin`, positions and frames along the track |
| `machine.c` | machine physics, AI, collisions, pickups |
| `camera.c` | chase, intro, finish, crash and replay cameras |
| `race.c` | race flow (`Race_Update`): start, laps, finish, crash, pause, grand prix progress |
| `scene.c` | the race scene and its rendering: course, objects, HUD; also the attract-mode demo |
| `effects.c`, `fx.c` | sparks, smoke, scenery; the additive effects (engine flame, pickups, messages) |
| `hud.c` | the animated centre messages (Start, Goal, ...) |
| `doors.c` | the two-part shutter that closes between screens |
| `title.c` | the "TN Production" intro |
| `menu.c` | the menus: modes, machines, stages, courses, options, password |
| `ending.c` | the staff roll |
| `save.c`, `replay.c` | `gamedata.dat` and the replay files, in the original formats |
| `settings.c` | settings added by the port (field of view, volumes) |
| `course_objects.c` | the gates and buildings of each course, extracted from the executable (generated, committed) |

## Requirements

All of this is for a Linux host (developed on Debian 13).

**Game data conversion** (needed for both targets):

- Python 3 with [Pillow](https://python-pillow.org/)
- A Python virtual environment at `re/.venv` with
  [Unicorn](https://www.unicorn-engine.org/) (and Capstone for the disassembly
  helper), used to extract the courses from the executable:

  ```sh
  python3 -m venv re/.venv
  re/.venv/bin/pip install unicorn capstone
  ```

- `fluidsynth`, `vorbis-tools` and a General MIDI soundfont, to pre-render the
  MIDI music (the PSP has no MIDI synthesizer):

  ```sh
  sudo apt install fluidsynth vorbis-tools fluid-soundfont-gm
  ```

**Linux build:** a C compiler, CMake ≥ 3.16, SDL2 and OpenGL development files
(`sudo apt install build-essential cmake libsdl2-dev libgl-dev`).

**PSP build:** the [pspdev toolchain](https://github.com/pspdev/pspdev)
(psp-gcc, the PSP SDK, `pack-pbp`, `mksfo`), with its `bin` folder in `PATH`,
for example `export PATH=$HOME/pspdev/bin:$PATH`.

**Testing on PC:** the [PPSSPP](https://www.ppsspp.org/) emulator. Set its
rendering resolution to 1× to see what a real PSP shows.

## Building

First put your copy of the original game folder at `game/` in the repository
root, so that `game/AceSpeeder.exe` exists.

### PSP (everything in one step)

```sh
cd port
export PATH=$HOME/pspdev/bin:$PATH
tools/make_dist.sh
```

`make_dist.sh` does the whole job:

1. extracts the 36 courses from the executable into `data/track_*.bin`
   (`re/scripts/extract_tracks.py`, only if they are missing);
2. renders the 8 MIDI songs to `data/*.ogg` (`tools/midi2ogg.sh`, only if missing),
   and reads the password check into `data/unlock.dat` (`tools/extract_unlock.py`,
   see [The password](#the-password));
3. makes the menu icon and background `ICON0.PNG` and `PIC1.PNG` from the game's
   own bitmaps (`tools/make_icons.py`);
4. builds `EBOOT.PBP` with `Makefile.psp`;
5. assembles `dist/PSP/GAME/AceSpeeder/`: the EBOOT, the game's meshes, bitmaps,
   sounds, replays and save, and the converted data.

### PSP (step by step)

```sh
cd port
../re/.venv/bin/python ../re/scripts/extract_tracks.py   # data/track_*.bin
tools/midi2ogg.sh ../game data                        # data/*.ogg
python3 tools/extract_unlock.py                       # data/unlock.dat
python3 tools/make_icons.py ../game .                 # ICON0.PNG, PIC1.PNG
make -f Makefile.psp                                  # EBOOT.PBP
```

`make -f Makefile.psp APP=viewer` or `APP=trackview` builds the debug programs
instead of the game. `make -f Makefile.psp clean` removes the build files.

### Linux

```sh
cd port
../re/.venv/bin/python ../re/scripts/extract_tracks.py   # if data/ is empty
tools/midi2ogg.sh ../game data                        # optional: music
python3 tools/extract_unlock.py                       # for the Password screen
cmake -S . -B build
cmake --build build
```

Run it from `port/`, telling it where the data is:

```sh
ACESPEEDER_DATA=data:../game build/acespeeder
```

### Android

The Android app uses the sdl3 backend (SDL3 and OpenGL ES 3, arm64-v8a). Build
it on Linux or macOS with the Android SDK and NDK (`ANDROID_HOME`, or `sdk.dir`
in `port/android/local.properties`) and a JDK 17 or newer; Gradle comes with
`android/gradlew`. The data conversion needs the same tools as the PSP build.

```sh
git submodule update --init      # port/third_party/SDL
cd port
tools/make_android.sh            # data/ if missing, then the APK
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The game's files and the generated data are copied into the APK. Installing,
controls and debugging on a device: [port/android/README.md](port/android/README.md).

### macOS (sdl3 backend)

The same backend runs on macOS (OpenGL 4.1 core), which is handy for
working on it:

```sh
git submodule update --init
cd port
cmake -S . -B build/sdl3 -DACESPEEDER_PLATFORM=sdl3
cmake --build build/sdl3
ACESPEEDER_DATA=data:../game build/sdl3/acespeeder
```

## Where the outputs go

| Output | Location |
|---|---|
| Extracted courses | `port/data/track_<stage>_<course>.bin` |
| Pre-rendered music | `port/data/*.ogg` |
| Password check | `port/data/unlock.dat` |
| PSP executable | `port/EBOOT.PBP` (and the intermediate `port/game.elf`, `port/PARAM.SFO`) |
| PSP menu images | `port/ICON0.PNG`, `port/PIC1.PNG` |
| **Packaged PSP game** | **`port/dist/PSP/GAME/AceSpeeder/`** |
| Linux game | `port/build/acespeeder` |
| Linux debug programs | `port/build/viewer`, `port/build/trackview`, `port/build/replaycheck` |
| Android APK | `port/android/app/build/outputs/apk/debug/app-debug.apk` |
| Library for the differential tests | `port/build/libgametest.so` |
| Saves on Linux | `~/.local/share/acespeeder/` (or `$ACESPEEDER_SAVE`) |
| Saves on the PSP | `PSP/SAVEDATA/ACESPEED00000/` (system save data) |

None of these are committed: they are built from your copy of the game.

## Putting the game on a PSP

A real PSP only runs homebrew such as this port with **custom firmware**
installed (for example ARK-4, or PRO / LME on 6.60–6.61). Setting that up is
outside the scope of this project.

1. Build the package with `tools/make_dist.sh` (see above).
2. Connect the PSP over USB (Settings → USB Connection), or put its Memory Stick
   in a card reader.
3. Copy the **whole** `port/dist/PSP` folder to the root of the Memory Stick,
   merging it with the `PSP` folder already there. The result must be:

   ```
   <Memory Stick>/PSP/GAME/AceSpeeder/EBOOT.PBP
   <Memory Stick>/PSP/GAME/AceSpeeder/*.x, *.bmp, *.wav, *.ogg, track_*.bin, ...
   ```

   (On a PSP Go with no Memory Stick, use the internal storage the same way.)
4. On the PSP, go to Game → Memory Stick and start **AceSpeeder!**.

The game needs all of the files in that folder, not only `EBOOT.PBP`. The data
files may also go in a `data` subfolder next to the EBOOT. If something is
missing, the game shows a screen listing the missing files instead of a black
screen.

### Saving on the PSP

The PSP version uses the system's save data screens, like a commercial game:

- When the game starts, it asks to **load** the save data. If there is none,
  or you go back, the game starts fresh, with the replays that ship with it and
  locked like the original: enter the password on the Password screen to unlock
  the extra courses and machines.
- It asks to **save** after a change worth keeping: when you leave the Setting
  screen with changed settings, after the right password, at the end of a
  grand prix (cleared, game over or quit), and after a time attack race that set
  a record or wrote a replay. If nothing changed, it does not ask.

Records, ranks, options, the unlock, `settings.txt` and the replays are kept
together in one save, `PSP/SAVEDATA/ACESPEED00000`, which you can copy or
delete from the PSP's Saved Data menu. The `save/` folder in the game folder
only holds debug logs; `save/savedata.log` records the result of each load and
save, which helps if saving fails on your console.

**On PPSSPP**, copy the `AceSpeeder` folder into PPSSPP's `PSP/GAME` folder
(for the Flatpak, `~/.var/app/org.ppsspp.PPSSPP/config/ppsspp/PSP/GAME/`), or
open `EBOOT.PBP` directly from the folder in `dist`.

## Playing

| PSP | Linux | Android gamepad | Action |
|---|---|---|---|
| D-pad / analog stick | arrows | D-pad / left stick | steer, move in the menus |
| Cross | Space / Enter | A (bottom), R2 | accelerate, confirm |
| Circle | Shift | B (right) | back; pause (the original's key) |
| Start | P | Start | pause and resume a race, confirm in the menus |
| Triangle | F1–F4 | Y, X, L1, R1 (F1–F4) | change the camera view |
| Select | F12 | Select | show the performance meter |

On Android the system Back button is the original's Esc.

The performance meter shows, averaged over 30 frames, the time spent in the game
logic, in building the frame and waiting for the GPU, and the frame rate.

### Settings added by the port

The Setting screen in the menu has a third row, **FOV**, which widens the race
camera's field of view (Original, Wide, Wider, Widest; Wide by default), and a
fourth, **Language**, which shows the game's Japanese text (menus, pause
screen, staff roll) in English or Japanese (English by default). They are
saved with the music and sound effect volumes in `settings.txt`, next to the
saves on Linux (inside the save data on the PSP):

```
fov=1
music=100
sfx=60
lang=en
```

`ACESPEEDER_LANG=ja` or `en` overrides the language for one run.

The volumes are percentages (0–200) and can be edited by hand.

### The password

As in the original, the extra stages and machines are unlocked by entering a
password in **Password** on the main menu; the author gives it out (see the
game's manual). The author asked that the password is never published, so it is
not in this repository: `tools/extract_unlock.py` reads the check from your own
`AceSpeeder.exe` into `data/unlock.dat` at build time. Without that file no
password is accepted. Please do not commit or share `unlock.dat`.

### Differences from the original

- The music is the original MIDI rendered with a General MIDI soundfont, so the
  instruments sound different from the Windows MIDI synthesizer.
- The coloured bands along each course are fixed; the original seeds its random
  numbers from the clock and picks new ones every race.
- On the PSP, the logic advances once every second screen refresh: 29.97 times a
  second against the original's 30.3, about 1% slower.
- On the PSP, the HUD and text designed for 640×480 are scaled to 480×272.
- The text is in English unless Language is set to Japanese; the English is
  the port's translation (the staff roll uses the original's own English
  cards).

## Development

### Running the Linux build

`build/acespeeder <stage> <course> <machine> <mode>` starts straight in a race
(1-based numbers; mode 0 grand prix, 1 practice, 2 replay). On the PSP the same
four numbers can be put in a `start.txt` file in the game folder.

Environment variables for debugging, all described at the top of
`port/src/main.c`:

| Variable | Effect |
|---|---|
| `ACESPEEDER_DATA` | data folders, separated by `:` |
| `ACESPEEDER_SAVE` | save folder |
| `ACESPEEDER_SHOTS=t1,t2,...` | run one logic tick per frame, save `shot_<tick>.ppm` at those ticks, then quit (`shots.txt` on the PSP, which also skips the save data dialog) |
| `ACESPEEDER_KEYS=t:k,t1-t2:k,...` | scripted key presses (`u d l r a b`, `s` Start, `f` meter, `1`–`4` views F1–F4; `keys.txt` on the PSP) |
| `ACESPEEDER_RATE` | every 5 seconds, log ticks and frames per second and the milliseconds per frame spent in the player's and opponents' physics, the world (scenery, gates, sparks), building the 3D scene and the clipping inside it (`rate.txt` on the PSP, logged to `save/rate.log`) |
| `ACESPEEDER_STATE` | start in another game state, e.g. `300` demo, `700` staff roll |
| `ACESPEEDER_CLIP=<guard>` | run the PSP's software clipping on Linux, to test it |
| `ACESPEEDER_SCALE` | window size multiplier |

### Differential tests

The scripts in `re/scripts/difftest_*.py` run functions of the original
executable under the Unicorn emulator and the port's versions from
`port/build/libgametest.so`, with the same random inputs, and compare every
value they touch:

```sh
cd port && cmake --build build && cd ..
re/.venv/bin/python re/scripts/difftest_raceupdate.py
```

| Test | Covers |
|---|---|
| `difftest_machine.py` | machine physics, pose, collisions |
| `difftest_race.py` | collisions, ranking, crash, machine init, launch, best lap |
| `difftest_camera.py` | all the cameras |
| `difftest_effects.py` | centre message quads, sparks, explosion, scenery, gates |
| `difftest_raceupdate.py` | the whole race flow, all phases |
| `difftest_setup.py` | race reset and starting grid |

### Reverse engineering

`re/run.sh` rebuilds the Ghidra project and the annotated decompilation in
`re/decomp/` from the executable. The decompilation is not in git (it is the
author's code, recovered); run the script on your copy of the game to get it. It needs Ghidra
(`GHIDRA=~/ghidra_12.1.4_PUBLIC` by default) and applies the scripts and name
lists in `re/`. The findings are written up in `re/NOTES.md`.

Data that the original computes in code is extracted by running that code in
the emulator: `extract_tracks.py` builds the courses (`port/data/track_*.bin`)
and `extract_objects.py` the gates and buildings
(`port/src/game/course_objects.c`). `port/tools/make_font.py` builds the
font from the game's own strings.
