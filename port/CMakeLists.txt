cmake_minimum_required(VERSION 3.16)
project(AceSpeeder C)

set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Which platform backend: linux (SDL2 + OpenGL 1.x, the default) or sdl3
# (SDL3 + OpenGL ES 3 / OpenGL 4.1 core, for Android and macOS; SDL3 comes
# from the third_party/SDL submodule). Android always uses sdl3.
set(ACESPEEDER_PLATFORM "linux" CACHE STRING "Platform backend: linux or sdl3")
if(ANDROID)
  set(ACESPEEDER_PLATFORM "sdl3" CACHE STRING "" FORCE)
endif()

if(ACESPEEDER_PLATFORM STREQUAL "sdl3")
  # Android's SDLActivity loads libSDL3.so and libmain.so, so SDL is shared
  # there; elsewhere it is linked in statically.
  if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
    message(FATAL_ERROR "third_party/SDL is empty: git submodule update --init")
  endif()
  add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(PLATFORM_SRC src/platform/sdl3/plat_sdl3.c)
else()
  find_package(SDL2 REQUIRED)
  find_package(OpenGL REQUIRED)
  set(PLATFORM_SRC src/platform/linux/plat_linux.c)
endif()

set(ENGINE_SRC
  src/engine/rmath.c
  src/engine/xfile.c src/engine/font.c src/engine/lang.c src/engine/sprite.c src/engine/prof.c
  src/engine/image.c
  src/engine/rm.c
  src/platform/mixer.c
  src/platform/clip.c
  ${PLATFORM_SRC}
)

add_library(engine STATIC ${ENGINE_SRC})
target_compile_options(engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
# stb_vorbis is third-party code: don't drown our warnings in its own
set_source_files_properties(src/platform/mixer.c PROPERTIES COMPILE_OPTIONS "-w")
# The game logic calls rmath.c, so it gets the game's -ffp-contract=off too (as
# in gametest below): clang on arm64 would otherwise fuse multiply-adds.
set_source_files_properties(src/engine/rmath.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
if(ACESPEEDER_PLATFORM STREQUAL "sdl3")
  target_compile_definitions(engine PUBLIC ACESPEEDER_SDL3)
  target_link_libraries(engine PUBLIC SDL3::SDL3 m)
  if(ANDROID)
    target_link_libraries(engine PUBLIC GLESv3 log)
  elseif(APPLE)
    target_link_libraries(engine PUBLIC "-framework OpenGL")
  else()
    target_link_libraries(engine PUBLIC GLESv2)
  endif()
else()
  target_include_directories(engine PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(engine PUBLIC SDL2::SDL2 OpenGL::GL m)
endif()

set(GAME_SRC src/game/gmath.c src/game/track.c src/game/machine.c src/game/camera.c src/game/effects.c src/game/hud.c src/game/race.c src/game/save.c src/game/replay.c)
# The original's physics ran in single precision: no fused/contracted operations.
set(GAME_FLAGS -Wall -Wextra -Wno-unused-parameter -ffp-contract=off)

add_library(game STATIC ${GAME_SRC} src/game/sound.c src/game/fx.c src/game/scene.c src/game/course_objects.c src/game/doors.c src/game/title.c src/game/menu.c src/game/ending.c src/game/settings.c)
target_link_libraries(game PUBLIC engine)
target_compile_options(game PRIVATE ${GAME_FLAGS})

if(ANDROID)
  # SDLActivity loads the game as libmain.so (see android/). The replay check
  # is built in: an Android executable has no window, so main.c runs it when
  # replaycheck.txt is in the app's external files folder.
  add_library(main SHARED src/main.c src/tools/replaycheck.c)
  target_compile_definitions(main PRIVATE REPLAYCHECK_IN_GAME)
  target_link_libraries(main PRIVATE game)
  target_compile_options(main PRIVATE ${GAME_FLAGS})
  return()
endif()

# Game logic as a shared library for the differential tests in re/scripts.
add_library(gametest SHARED ${GAME_SRC} src/tools/testlib.c src/engine/rmath.c src/engine/prof.c)
target_compile_options(gametest PRIVATE ${GAME_FLAGS} -DGAME_TESTLIB)
target_link_libraries(gametest PRIVATE m)

add_executable(acespeeder src/main.c)
target_link_libraries(acespeeder game)
target_compile_options(acespeeder PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(viewer src/tools/viewer.c)
target_link_libraries(viewer engine)

add_executable(trackview src/tools/trackview.c)
target_link_libraries(trackview game)

# Plays the shipped replays without drawing and prints how each one finishes.
add_executable(replaycheck src/tools/replaycheck.c)
target_link_libraries(replaycheck game)
target_compile_options(replaycheck PRIVATE ${GAME_FLAGS})
