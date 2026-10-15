# Linux build of the co-op player (needs libsdl2-dev and libmgba-dev).
# The auto updater is Windows-only; here it does nothing.
VERSION ?= $(shell cat ../VERSION)
zelda-blend: coop.c updater.c updater.h
	$(CC) -O2 -Wall -DPICORI_VERSION='"$(VERSION)"' -o $@ coop.c updater.c $(shell sdl2-config --cflags --libs) -lmgba
