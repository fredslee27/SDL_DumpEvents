RM=rm

SDL_DumpEvents: src/SDL_DumpEvents.c
# Be sure to have backslash-doublequote so CPP sees a string literal.
	$(CC) -DHAVE_GETOPT_LONG=1 -DBUILDIN_TTF=\"src/FreeMono.ttf\"  -std=c99 -Wall -Wextra -Wstrict-prototypes -pedantic  -o $@ $< `pkg-config --cflags --libs sdl2 SDL2_ttf`

clean:
	$(RM) -f SDL_DumpEvents

