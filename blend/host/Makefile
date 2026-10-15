# Linux build of the co-op player (needs libsdl2-dev and libmgba-dev)
zelda-blend: coop.c
	$(CC) -O2 -Wall -o $@ $< $(shell sdl2-config --cflags --libs) -lmgba
