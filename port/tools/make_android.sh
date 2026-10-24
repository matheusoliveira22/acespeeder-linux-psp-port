#!/bin/sh
# Build the Android APK (sdl3 backend, arm64-v8a). Needs the original game
# folder, the Android SDK and NDK (ANDROID_HOME, or sdk.dir in
# android/local.properties), a JDK 17 or newer and, as for the PSP, the
# extraction tools and fluidsynth/oggenc for the data. Gradle comes with
# android/gradlew.
# Usage (from port/): tools/make_android.sh [game dir]
# The APK is android/app/build/outputs/apk/debug/app-debug.apk.
set -e
GAME=${1:-../game}

[ -f data/track_1_1.bin ] || ../re/.venv/bin/python ../re/scripts/extract_tracks.py
ls data/*.ogg > /dev/null 2>&1 || tools/midi2ogg.sh "$GAME" data
[ -f data/unlock.dat ] || python3 tools/extract_unlock.py "$GAME/AceSpeeder.exe" data/unlock.dat

GAME_ABS=$(cd "$GAME" && pwd)
DATA_ABS=$(cd data && pwd)
cd android
./gradlew -q assembleDebug -PaceSpeederGameDir="$GAME_ABS" -PaceSpeederGeneratedDir="$DATA_ABS"
echo "android/app/build/outputs/apk/debug/app-debug.apk: $(du -h app/build/outputs/apk/debug/app-debug.apk | cut -f1)"
