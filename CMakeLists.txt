cmake_minimum_required(VERSION 3.20)
project(DoomRPG C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SDL2 supplies display / input / audio. Easiest path on Windows is vcpkg:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
find_package(SDL2 CONFIG QUIET)

# Hand-written J2ME runtime.
file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS runtime/src/*.c)

# Recompiler output (regenerate with: python tools/recompiler/jrecomp.py translate ...).
file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS generated/*.c)

if(NOT GENERATED_SOURCES)
    message(WARNING
        "generated/ is empty -- run the recompiler before building the game:\n"
        "    python tools/recompiler/jrecomp.py translate game/DoomRPG.jar -o generated/")
endif()

add_executable(DoomRPG ${RUNTIME_SOURCES} ${GENERATED_SOURCES})
target_include_directories(DoomRPG PRIVATE runtime/include generated)

if(SDL2_FOUND)
    target_link_libraries(DoomRPG PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2>
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>)
endif()

if(WIN32)
    # Windows MIDI playback (midiStream) lives in winmm.
    target_link_libraries(DoomRPG PRIVATE winmm)
endif()
