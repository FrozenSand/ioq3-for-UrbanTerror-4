#!/bin/sh

export CC=i586-mingw32msvc-gcc
# this doesn't generate binaries, only code I believe, so it's fine. I didn't have i586-mingw32msvc-windres on my system
export WINDRES=i686-w64-mingw32-windres
export PLATFORM=mingw32
exec make $*
