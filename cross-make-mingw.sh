#!/bin/sh
exec make PLATFORM=mingw32 CC=i686-w64-mingw32-gcc LD=i686-w64-mingw32-ld WINDRES=i686-w64-mingw32-windres $*

