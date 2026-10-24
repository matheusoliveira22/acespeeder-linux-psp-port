# AceSpeeder! for Android

The Android app: the port's game and engine with the sdl3 backend
(`port/src/platform/sdl3/plat_sdl3.c`, SDL3 and OpenGL ES 3), arm64-v8a.
Developed on an AYN Thor (Android 13, Snapdragon 8 Gen 2, Adreno 740, built-in
controller); any arm64 device with OpenGL ES 3 and a gamepad should do.

## Building

`tools/make_android.sh` (from `port/`) converts the data if `port/data/` is
missing it and builds the APK; see the main README. By hand:

```sh
cd port/android
./gradlew assembleDebug -PaceSpeederGameDir=/path/to/game -PaceSpeederGeneratedDir=/path/to/port/data
```

The two folders default to the repository's `game/` and `port/data/`, and can
also be set as `aceSpeederGameDir` and `aceSpeederGeneratedDir` in
`local.properties`, next to `sdk.dir` (the Android SDK, if `ANDROID_HOME` is
not set) and, optionally, `cmake.dir` (a CMake of your own instead of the
SDK's). Versions: Android Gradle plugin 9.4.1 (Gradle 9.8 through the
wrapper), compileSdk and targetSdk 36, build-tools 36.0.0, NDK 30, minSdk 24.

How it fits together:

- `app/build.gradle` follows SDL's `android-project` template and takes SDL's
  Java glue straight from `port/third_party/SDL`. Its native build is
  `port/CMakeLists.txt`, which on Android builds SDL3 as `libSDL3.so` and the
  game as `libmain.so`, the two libraries `SDLActivity` loads.
  `AceSpeederActivity` is an empty `SDLActivity` subclass.
- The `copyGameAssets` task copies what the game loads (the original's `.x`,
  `.bmp`, `.wav` and `replay_*.dat`; not `gamedata.dat`, the player's save) and
  the generated data (`track_*.bin`, `*.ogg`, `unlock.dat`) into the APK's
  `assets/data/`. The backend reads them through `SDL_IOFromFile` wrapped in a
  `FILE *` (`funopen`), so the game's stdio code reads APK assets unchanged.
  The APK is about 22 MB. Like `unlock.dat`, it contains the original game's
  files: keep it for personal use.
- Saves (`gamedata.dat`, `settings.txt`, `rate.log`) go to the app's internal
  files folder (`adb shell run-as org.acespeeder.port ls files` on a debug
  build).
- Landscape only, fullscreen, the screen kept on; audio pauses while the app is
  in the background. On a 1920x1080 screen the game is 1440x1080 with black
  bars, as on Linux.

## Installing

Enable USB debugging (Settings, About, tap the build number seven times, then
Developer options, USB debugging), connect the device and accept the prompt:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.acespeeder.port/.AceSpeederActivity
adb logcat -s SDL SDL/APP
```

Wireless debugging works too: `adb pair <ip>:<pairing port>` with the six-digit
code, then `adb connect <ip>:<port>` (a different port, shown on the main
Wireless debugging screen).

## Playing

The controls are in the main README. On the Thor SDL reports the controller as
"Odin Controller" (2020:0111) with its built-in mapping, and every button
arrives as listed there. The system Back button sets the original's Esc key,
which the game does not use: B goes back and Start pauses. The thin white bar at
the right edge of the Thor's screen is the system's overlay handle for its
on-screen control mapping, not the game.

## Debug files

Files in the app's external files folder,
`/sdcard/Android/data/org.acespeeder.port/files/`, are found before the APK's
assets, and the game's working folder is that folder too. Push one there
(`adb push`), restart the app (`adb shell am force-stop org.acespeeder.port`
first) and delete it to play normally again:

| File | Effect |
|---|---|
| `start.txt` | `stage course machine mode` straight into a race, as the Linux build's arguments |
| `keys.txt`, `shots.txt` | scripted key presses and screenshots, as `ACESPEEDER_KEYS` and `ACESPEEDER_SHOTS` (see the top of `src/main.c`); `adb pull` the `shot_<tick>.ppm` files from the same folder |
| `rate.txt` | log ticks, frames and milliseconds per frame every 5 seconds to `rate.log` |
| `replaycheck.txt` | play the shipped replays without drawing instead of starting the game: the stage/course pairs listed (`1 3 2 4`), or all 36 if empty; one line per replay in `adb logcat -s SDL/APP` |

`replaycheck` is built into `libmain.so` because an Android executable has no
window or GL context. To compare a device with a desktop build:

```sh
adb logcat -d -s SDL/APP | cut -d: -f4- | grep -E '^ *[0-9]-[0-9]' | sed 's/^ //' > android.txt
ACESPEEDER_SAVE=$(mktemp -d) ACESPEEDER_DATA=data:../game build/sdl3/replaycheck > desktop.txt
diff desktop.txt android.txt
```

On the Thor all 36 results matched the macOS build's.

## Performance on the Thor

The game runs logic and drawing once per 33 ms tick, so 30 frames a second is
its full rate. In a race the meter (Select) reads logic 0.1-0.2 ms, drawing
about 10 ms and the GPU 3 ms. Streaming vertices with `glMapBufferRange` for
every draw was very slow on Adreno (3 frames a second); the backend uses
`glBufferSubData` into one streamed buffer.
