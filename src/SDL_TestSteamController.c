/* gcc `pkg-config --cflags --libs sdl2 SDL2_ttf`
*/
/*
    Dump SDL events.
    Copyright (C) 2018  Fred Lee <fredslee27@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_ttf.h>

/*
   Original intent was to help test/debug Steam Controller configurations.

   Display SDL events into categorized columns:

MISC       | KEYB      | MOUSE     | JOY      | CONTROLLER
           |           |           |          |
...
*/


/* Application title - used in title bar and textual references to self. */
#define APP_TITLE "SDL_DumpEvents"
/* Default font file name. */
#define DEFAULT_FONT_FILENAME "FreeMono.ttf"
#define DIR_SEPARATOR "/"

/* Maximum length of one log line, bytes. */
#define MAX_LINELENGTH 256
/* Maximum number of log lines per category. */
#define MAX_NUMLINES 32
/* Max number of joystick devices to recognize. */
#define MAX_JOYSTICKS 8

/* Default window size. */
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720



/* major SDL Event type categorize for display:
  + a catch-all for types that do not fit the others (listed first due to including windowing events such as Show and Expose).
  + Keyboard events, including SDL_KEYDOWN and SDL_KEYUP.
  + Mouse events, including motion, buttons, and scroll wheel.
  + Joystick events, including .
  + SDL Controller events.
*/
enum {
    CAT_MISC,
    CAT_KEYB,
    CAT_MOUSE,
    CAT_JOY,
    CAT_CONTROLLER,

    MAX_CATEGORIES
};


/* One log entry line. */
typedef struct logentry_s {
    char line[MAX_LINELENGTH];
    SDL_Surface * surf;
    SDL_Texture * tex;
} logentry_t;

/* List of logentry instances. */
typedef struct logbuf_s {
    int cap;  // maximum lines permitted.
    int len;  // current lines valid.
    int head;  // ring buffer.

    logentry_t buf[MAX_NUMLINES];
} logbuf_t;

/* Persistent graphics elements. */
typedef struct gfxdecor_s {
    int x;
    int y;
    SDL_Surface * surf;
    SDL_Texture * tex;
} gfxdecor_t;

typedef struct app_s {
    int alive;

    int width;
    int height;
    int wflags;
    int x0;
    int y0;
    SDL_Window * w;
    int rindex;  /* initial renderer; -1 (auto-detect) by default. */
    int rflags;
    SDL_Renderer *r;
    SDL_GLContext glctx;

    SDL_RWops * font_io[1];  /* SDL_RWops* type for TTF (file). */
    TTF_Font * fonts[4];
    int njs;  // number of joysticks opened.
    SDL_Joystick * jspack[MAX_JOYSTICKS];

    /* Persistent decorations. */
    gfxdecor_t decor[30];

    /* A logbuf instance per column. */
    logbuf_t logbuf[MAX_CATEGORIES];

    char title0[255];
} app_t;




#ifdef __GNUC__
/* This bit of assembly incorporates the contents of the default font file, yielding symbols 'ttf0_start', 'ttf0_end', 'ttf0_size'.
Values intended to mimick the effect of using "ld -r -b binary ..." or objcopy to include arbitrary binary file into object file.
*/
__asm__(
	".global _binary_ttf0_start\n.global _binary_ttf0_end\n.global _binary_ttf0_size\n"
	"_binary_ttf0_start: .incbin \"" DEFAULT_FONT_FILENAME "\"\n"
	"_binary_ttf0_end: .byte 0\n"
	".set _binary_ttf0_size, (_binary_ttf0_end - _binary_ttf0_start)\n"
);
extern const unsigned char _binary_ttf0_start[];
extern const unsigned char _binary_ttf0_end[0];
extern const struct{} _binary_ttf0_size;

/* Aliases/casts for C semantics. */
const unsigned char * ttf0_data = _binary_ttf0_start;
const unsigned char * ttf0_data_end = _binary_ttf0_end;
#define ttf0_size ((long)&_binary_ttf0_size)
#else
/* no GNU asm; use zeroes. */
const unsigned char ttf0_data[1] = { 0 };
const unsigned char * ttf0_data_end = ttf0_data;
#define ttf0_size (0)
#endif //0



const char BANNER[] = APP_TITLE " - add as Non-Steam Game, run from Big Picture Mode";


logbuf_t * logbuf_init (logbuf_t * logbuf, int cap)
{
  if (!logbuf)
    {
      logbuf = SDL_malloc(sizeof(logbuf_t));
    }
  if (! logbuf)
    {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "failed malloc in logbuf.new");
      abort();
    }

  SDL_memset(logbuf, 0, sizeof(logbuf));
  if (cap <= 0)
    {
      cap = MAX_NUMLINES;
    }
  logbuf->cap = cap;
}

logbuf_t * logbuf_destroy (logbuf_t * logbuf)
{
  return logbuf;
}

/* append line to buffer. */
int logbuf_append (logbuf_t * logbuf, const char * buf, int buflen)
{
  int n = (logbuf->head + logbuf->len) % logbuf->cap;
  SDL_memcpy(logbuf->buf[n].line, buf, buflen);
  logbuf->len++;
  if (logbuf->len > logbuf->cap)
    {
      /* Overflowed. */
      logentry_t * entry = logbuf->buf + logbuf->head;
      SDL_FreeSurface(entry->surf);
      entry->surf = NULL;
      SDL_DestroyTexture(entry->tex);
      entry->tex = NULL;

      logbuf->head = (logbuf->head + 1) % logbuf->cap;
      logbuf->len--;
    }
}

int logbuf_len (logbuf_t * logbuf)
{
  return logbuf->len;
}

logentry_t * logbuf_get (logbuf_t * logbuf, int nth)
{
  if (nth >= logbuf->len)
    return NULL;
  if (nth < 0)
    nth = (logbuf->len + nth);
  int ofs = logbuf->head + nth;
  if (ofs >= logbuf->cap)
    ofs %= logbuf->cap;
  return logbuf->buf + ofs;
}


int logbuf_test ()
{
  logbuf_t _logbuf, *logbuf=&_logbuf;
  logbuf = logbuf_init(logbuf, 3);

  int i;
  for (i = 0; i < 16; i++)
    {
      char msg[64];
      int res = SDL_snprintf(msg, sizeof(msg), "Line %d", i);
      logbuf_append(logbuf, msg, res);
    }
  printf("DUMP:\n");
  for (i = 0; i < 8; i++)
    {
      const logentry_t * entry = logbuf_get(logbuf, i);
      const char * line = entry->line;
      if (line)
	{
	  printf(" %d: %s\n", i, line);
	}
    }
}



/*
   Determine fully-qualified path name to font file 'filename'.
Try:
1. environment variable(s) -- user-modifiable
2. SDL's notion of the application data directory -- fallback/default
3. current working directory -- last resort
*/
static
SDL_RWops * find_path_to_ttf_file (const char * filename, char * out_buf, int buflen)
{
  SDL_RWops * retval = NULL;
  int n = 0;

  /* try env vars. */
  const char * env_name = "SDL_DUMPEVENTS_PATH";
  n = 0;
  n += SDL_snprintf(out_buf+n, buflen-n, "%s%s", getenv(env_name), DIR_SEPARATOR);
  n += SDL_snprintf(out_buf+n, buflen-n, "%s", filename);
  retval = SDL_RWFromFile(out_buf, "rb");
  if (retval) return retval;

  /* try SDL BasePath */
  char * sdl_basepath = SDL_GetBasePath();
  if (sdl_basepath)
    {
      n = 0;
      n += SDL_snprintf(out_buf+n, buflen-n, "%s%s", sdl_basepath, filename);
      retval = SDL_RWFromFile(out_buf, "rb");
      SDL_free(sdl_basepath);
      if (retval) return retval;
    }

  /* try cwd */
  if ((retval = SDL_RWFromFile(filename, "r"))) return retval;

  return NULL;
}



app_t * app_init (app_t * app)
{
  if (!app)
    {
      app = SDL_malloc(sizeof(app_t));
    }
  if (!app)
    {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "failed malloc in app.new");
      abort();
    }
  SDL_memset(app, 0, sizeof(*app));

  int i;

  for (i = 0; i < MAX_CATEGORIES; i++)
    {
      logbuf_init(app->logbuf + i, 0);
    }

  SDL_Init(SDL_INIT_EVERYTHING);

  /* open main window. */
  SDL_snprintf(app->title0, sizeof(app->title0), APP_TITLE);
  app->width = DEFAULT_WIDTH;
  app->height = DEFAULT_HEIGHT;
  app->x0 = SDL_WINDOWPOS_UNDEFINED;
  app->y0 = SDL_WINDOWPOS_UNDEFINED;
  app->wflags = SDL_WINDOW_OPENGL;
  app->rindex = 0;
  app->rflags = 0;

  app->w = SDL_CreateWindow(app->title0,
			    app->x0, app->y0,
			    app->width, app->height,
			    app->wflags);
  app->r = SDL_CreateRenderer(app->w, -1, app->rflags);

  /* load fonts. */
  TTF_Init();

  if (ttf0_size)
    {
      /* use built-in font. */
      app->font_io[0] = SDL_RWFromMem((void*)ttf0_data, ttf0_size);
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Using built-in font.");
    }
  else
    {
      /* search for suitable font file. */
#ifdef _PC_PATH_MAX
      const int max_pathlen = pathconf("/", _PC_PATH_MAX);
#else
      const int max_pathlen = 4096; /* something sensible as of Y2013. */
#endif /* _PC_PATH_MAX */
      char * fqpn_font = SDL_malloc(max_pathlen);
      app->font_io[0] = find_path_to_ttf_file(DEFAULT_FONT_FILENAME, fqpn_font, max_pathlen);
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Using font file '%s'", fqpn_font);
      SDL_free(fqpn_font);
    }

  if (! app->font_io[0])
    {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Unable to open any font file.");
      abort();
    }
  app->fonts[0] = TTF_OpenFontRW(app->font_io[0], 0/*do not auto-close*/, 12);
  app->fonts[1] = TTF_OpenFontRW(app->font_io[0], 0, 16);
  app->fonts[2] = TTF_OpenFontRW(app->font_io[0], 0, 20);

  /* Do not close the RW until TTF lib shuts down. */

  return app;
}

app_t * app_destroy (app_t * app)
{
  TTF_CloseFont(app->fonts[2]);
  TTF_CloseFont(app->fonts[1]);
  TTF_CloseFont(app->fonts[0]);
  SDL_RWclose(app->font_io[0]);
  TTF_Quit();

  SDL_DestroyRenderer(app->r);
  app->r = NULL;
  SDL_DestroyWindow(app->w);
  app->w = NULL;
  SDL_Quit();
  return app;
}


int app_write (app_t * app, int category, const char * msg)
{
  int n;
  n = SDL_strlen(msg)+1;
  logbuf_append(app->logbuf + category, msg, n);
  return 0;
}

int app_vfwrite (app_t * app, int category, const char * fmt, va_list vp)
{
  char buf[MAX_LINELENGTH];
  int res = SDL_vsnprintf(buf, sizeof(buf), fmt, vp);
  app_write(app, category, buf);
  return res;
}

int app_fwrite (app_t * app, int category, const char * fmt, ...)
{
  int retval = 0;
  va_list vp;
  va_start(vp, fmt);
  retval = app_vfwrite(app, category, fmt, vp);
  va_end(vp);
  return retval;
}


int app_on_quit (app_t * app, SDL_Event * evt)
{
  app_write(app, CAT_MISC, "QUIT");
  app->alive = 0;
  return 0;
}

int app_on_window (app_t * app, SDL_Event * evt)
{
  switch (evt->window.event)
    {
    case SDL_WINDOWEVENT_SHOWN:
      app_write(app, CAT_MISC, "WIN SHOW");
      break;
    case SDL_WINDOWEVENT_HIDDEN:
      app_write(app, CAT_MISC, "WIN HIDE");
      break;
    case SDL_WINDOWEVENT_EXPOSED:
      app_write(app, CAT_MISC, "WIN EXPOSE");
      break;
    case SDL_WINDOWEVENT_MOVED:
      app_write(app, CAT_MISC, "WIN MOVE");
      break;
    case SDL_WINDOWEVENT_RESIZED:
      app_write(app, CAT_MISC, "WIN RESIZE");
      break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
      app_write(app, CAT_MISC, "WIN SIZE CHANGE");
      break;
    case SDL_WINDOWEVENT_MINIMIZED:
      app_write(app, CAT_MISC, "WIN ICONIFY");
      break;
    case SDL_WINDOWEVENT_MAXIMIZED:
      app_write(app, CAT_MISC, "WIN MAXIMIZE");
      break;
    case SDL_WINDOWEVENT_RESTORED:
      app_write(app, CAT_MISC, "WIN RESTORED");
      break;
    case SDL_WINDOWEVENT_ENTER:
      app_write(app, CAT_MISC, "WIN ENTER");
      break;
    case SDL_WINDOWEVENT_LEAVE:
      app_write(app, CAT_MISC, "WIN LEAVE");
      break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
      app_write(app, CAT_MISC, "WIN FOCUS IN");
      break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
      app_write(app, CAT_MISC, "WIN FOCUS OUT");
      break;
    case SDL_WINDOWEVENT_CLOSE:
      app_write(app, CAT_MISC, "WIN CLOSE");
      break;
    default:
      break;
    }
}

/* handle KEYDOWN (keyboard key press) event. */
int app_on_keydown (app_t * app, SDL_Event * evt)
{
  if (evt->key.repeat)
    return 0;

  const char * keyname = SDL_GetKeyName(evt->key.keysym.sym);
  if (!keyname)
    {
      char altname[32];
      SDL_snprintf(altname, sizeof(altname), "(%d)", evt->key.keysym.sym);
      keyname = altname;
    }
  app_fwrite(app, CAT_KEYB, "PRESS: %s", keyname);
  return 0;
}

/* handle KEYUP (keyboard key release) event. */
int app_on_keyup (app_t * app, SDL_Event * evt)
{
  const char * keyname = SDL_GetKeyName(evt->key.keysym.sym);
  if (!keyname)
    {
      char altname[32];
      SDL_snprintf(altname, sizeof(altname), "(%d)", evt->key.keysym.sym);
      keyname = altname;
    }
  app_fwrite(app, CAT_KEYB, "RELEASE: %s", keyname);
  if (evt->key.keysym.sym == SDLK_ESCAPE)
    {
      app->alive = 0;
    }
  return 0;
}

/* handle MOUSEMOTION (mouse moving) event. */
int app_on_mousemove (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_MOUSE, "MV: %+d%+d:(%d,%d)",
	     evt->motion.xrel,
	     evt->motion.yrel,
	     evt->motion.x,
	     evt->motion.y
	     );
  return 0;
}

/* handle MOUSEBUTTONDOWN (mouse button press) event. */
int app_on_mousebdown (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_MOUSE, "PRESS: %d", evt->button.button);
  return 0;
}

/* handle MOUSEBUTTONUP (mouse button release) event. */
int app_on_mousebup (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_MOUSE, "RELEASE: %d", evt->button.button);
  return 0;
}

/* handle MOUSEWHEEL (mouse wheel) event. */
int app_on_mousewheel (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_MOUSE, "WHEEL: %+d%+d", evt->wheel.x, evt->wheel.y);
  return 0;
}


/* handle JOYAXISMOTION (joystick axis) event. */
int app_on_joyaxis (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_JOY, "%d/AXIS/%d: %d",
	     evt->jaxis.which,
	     evt->jaxis.axis,
	     evt->jaxis.value);
  return 0;
}

/* handle JOYHATMOTION (joystick hat) event. */
int app_on_joyhat (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_JOY, "%d/HAT/%d: %d",
	     evt->jhat.which,
	     evt->jhat.hat,
	     evt->jhat.value);
  return 0;
}

/* handle JOYBALLMOTION (joystick trackball) event. */
int app_on_joyball (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_JOY, "%d/BALL/%d: %+d%+d",
	     evt->jball.which,
	     evt->jball.ball,
	     evt->jball.xrel,
	     evt->jball.yrel);
  return 0;
}

/* handle JOYBUTTONDOWN (joystick button press) event. */
int app_on_joybdown (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_JOY, "%d/PRESS: %d",
	     evt->jbutton.which,
	     evt->jbutton.button);
  return 0;
}

/* handle JOYBUTTONUP (joystick button up) event. */
int app_on_joybup (app_t * app, SDL_Event * evt)
{
  return app_fwrite(app, CAT_JOY, "%d/RELEASE: %d",
	     evt->jbutton.which,
	     evt->jbutton.button);
  return 0;
}

/* handle joystick device events: connect, disconnect. */
int app_on_joydev (app_t * app, SDL_Event * evt)
{
  const char * action = "?";
  char jsname[80] = { 0, };
  int packidx = -1; /* opened joystick handles used by this app. */

  /* device number, queried from system. */
  int devnum = -1;
  /* Joystick instance id, used by SDL events reporting. */
  int instid = -1;

  switch (evt->jdevice.type)
    {
    case SDL_JOYDEVICEADDED:
      action = "ADD";
      /* add to open devices. */
      devnum = evt->jdevice.which; /* joystick device index. */
      /* find empty handle slot. */
      packidx = 0;
      while (packidx < MAX_JOYSTICKS)
	{
	  if (! app->jspack[packidx])
	    break;
	  packidx++;
	}
      if ((0 <= packidx) && (packidx < MAX_JOYSTICKS))
	{
	  /* valid slot, open device. */
	  SDL_Joystick * openjs = SDL_JoystickOpen(devnum);
	  app->jspack[packidx] = openjs;
	  if (openjs)
	    {
	      instid = SDL_JoystickInstanceID(openjs);
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Added joystick #%d from index %d as handle %d", instid, devnum, packidx);
	      SDL_snprintf(jsname, sizeof(jsname), "%s", SDL_JoystickName(openjs));
	    }
	  else
	    {
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Out of memory while tyring to open joystick #%d", devnum);
	    }
	}
      else
	{
	  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Unable to open joystick #%d.", devnum);
	}
      break;
    case SDL_JOYDEVICEREMOVED:
      action = "REMOVE";
      /* remove from open devices. */
      instid = evt->jdevice.which; /* joystick instance id. */
      /* find matching instid. */
      for (packidx = 0; packidx < MAX_JOYSTICKS; packidx++)
	{
	  if (! app->jspack[packidx])
	    continue;
	  SDL_Joystick * doomedjs = app->jspack[packidx];
	  if (SDL_JoystickInstanceID(doomedjs) == instid)
	    {
	      /* joystick number matches; close and remove from handles */
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Remove joystck handle %d being joystick #%d\n", packidx, instid);
	      SDL_snprintf(jsname, sizeof(jsname), "%s", SDL_JoystickName(doomedjs));
	      SDL_JoystickClose(app->jspack[packidx]);
	      app->jspack[packidx] = NULL;
	    }
	}
      break;
    }
  app_fwrite(app, CAT_JOY, "%s: %d = %s", action, instid, jsname);
  return 0;
}


/* handle CONTROLLERAXISMOTION (SDL Game Controller joystick) event. */
int app_on_gameaxis (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_CONTROLLER, "%d/AXIS/%d: %d",
	     evt->caxis.which,
	     evt->caxis.axis,
	     evt->caxis.value);
  return 0;
}

/* handle CONTROLLERBUTTONDOWN (SDL Game Controller button press) event. */
int app_on_gamebdown (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_CONTROLLER, "%d/PRESS: %d",
	     evt->cbutton.which,
	     evt->cbutton.button);
  return 0;
}

/* handle CONTROLLERBUTTONUP (SDL Game Controller button release) event. */
int app_on_gamebup (app_t * app, SDL_Event * evt)
{
  app_fwrite(app, CAT_CONTROLLER, "%d/RELEASE: %d",
	     evt->cbutton.which,
	     evt->cbutton.button);
  return 0;
}

/* handle SDL Game Controller device events: add, remove, remap. */
int app_on_gamedev (app_t * app, SDL_Event * evt)
{
  const char * action = "?";
  switch (evt->cdevice.type)
    {
    case SDL_CONTROLLERDEVICEADDED:
      action = "ADD";
      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      action = "REMOVE";
      break;
    case SDL_CONTROLLERDEVICEREMAPPED:
      action = "REMAP";
      break;
    }
  app_fwrite(app, CAT_CONTROLLER, "%s: %d",
	     action,
	     evt->cdevice.type);
  return 0;
}



int app_printxy (app_t * app, TTF_Font * fon, int x, int y, const char * msg)
{
  SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
  SDL_Surface * textsurf = TTF_RenderText_Blended(fon, msg, fg);
  SDL_Rect dst = { x, y, textsurf->w, textsurf->h };
  SDL_Texture * blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
  SDL_RenderCopy(app->r, blttex, NULL, &dst);

  SDL_DestroyTexture(blttex);
  SDL_FreeSurface(textsurf);

  return 0;
}

int app_install_text (app_t * app, int decor_idx, TTF_Font * fon, int x, int y, const char * msg)
{
  SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
  SDL_Texture * blttex = NULL;
  SDL_Surface * textsurf = app->decor[decor_idx].surf;

  if (!textsurf)
    {
      textsurf = TTF_RenderText_Blended(fon, msg, fg);
      blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
      SDL_Rect dst = { x, y, textsurf->w, textsurf->h };
      app->decor[decor_idx].x = x;
      app->decor[decor_idx].y = y;
      app->decor[decor_idx].surf = textsurf;
      app->decor[decor_idx].tex = blttex;
    }
  else
    {
      blttex = app->decor[decor_idx].tex;
    }

  SDL_Rect dst = { x, y, textsurf->w, textsurf->h };
  SDL_RenderCopy(app->r, blttex, NULL, &dst);

  return 0;
}

int app_cycle_gfx (app_t * app)
{
  SDL_SetRenderDrawColor(app->r, 0,0,0,0);
  SDL_RenderClear(app->r);

  SDL_SetRenderDrawColor(app->r, 0xff,0xff,0xff,0xff);

  /* banner text at top of surface. */
  app_install_text(app, 0, app->fonts[2], 0, 0, BANNER);

  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "len(logbuf) = %d", logbuf_len(app->logbuf));

  /* the categories drawn */
  static const char * catlabel[MAX_CATEGORIES] = {
      "MISC",
      "KEYB",
      "MOUSE",
      "JOY",
      "SDL_CONTROLLER",
  };
  int catnum;
  int x0 = 0;
  int y0 = 40;
  int x, y;
  for (catnum = 0; catnum < MAX_CATEGORIES; catnum++)
    {
      x = x0 + (catnum * app->width / MAX_CATEGORIES);
      y = y0;
      if (catnum > 0)
	{
	  // separator line.
	  SDL_RenderDrawLine(app->r, x-4, y, x-4, app->height);
	}

      /* Place category name as column header. */
      app_install_text(app, catnum+1, app->fonts[2], x, y, catlabel[catnum]);

      /* Sync rendered textsurf with log lines for this category. */
      int linenum;
      for (linenum = 0; linenum < MAX_NUMLINES; linenum++)
	{
	  y += 20;
	  logentry_t * entry = logbuf_get(app->logbuf + catnum, linenum);
	  if (!entry) continue;
	  SDL_Texture * blttex = entry->tex;
	  if (!blttex)
	    {
	      /* generate corresponding rendered textsurf. */
	      SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
	      TTF_Font * fon = app->fonts[2];
	      const char * msg = entry ? entry->line : NULL;
	      if (msg && *msg)
		{
		  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "create text %d,%d\n", catnum, linenum);
		  SDL_Surface * textsurf = TTF_RenderText_Blended(fon, msg, fg);
		  blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
		  entry->surf = textsurf;
		  entry->tex = blttex;
		}
	    }
	  if (blttex)
	    {
	      /* render corresponding textsurf. */
	      Uint32 fmt = 0;
	      int access = 0, w = 0, h = 0;
	      SDL_QueryTexture(blttex, &fmt, &access, &w, &h);
	      SDL_Rect dst = { x, y, w, h };
	      SDL_RenderCopy(app->r, blttex, NULL, &dst);
	    }
	}
    }

  SDL_RenderPresent(app->r);
}

int app_cycle (app_t * app)
{
  SDL_Event _evt, *evt=&_evt;
  while (SDL_PollEvent(evt) > 0)
    {
      switch (evt->type)
	{
	case SDL_QUIT:
	  app->alive = 0;
	  break;
	case SDL_WINDOWEVENT:
	  app_on_window(app, evt);
	  break;
	case SDL_KEYDOWN:
	  app_on_keydown(app, evt);
	  break;
	case SDL_KEYUP:
	  app_on_keyup(app, evt);
	  break;
	case SDL_MOUSEMOTION:
	  app_on_mousemove(app, evt);
	  break;
	case SDL_MOUSEBUTTONDOWN:
	  app_on_mousebdown(app, evt);
	  break;
	case SDL_MOUSEBUTTONUP:
	  app_on_mousebup(app, evt);
	  break;
	case SDL_MOUSEWHEEL:
	  app_on_mousewheel(app, evt);
	  break;
	case SDL_JOYAXISMOTION:
	  app_on_joyaxis(app, evt);
	  break;
	case SDL_JOYHATMOTION:
	  app_on_joyhat(app, evt);
	  break;
	case SDL_JOYBALLMOTION:
	  app_on_joyball(app, evt);
	  break;
	case SDL_JOYBUTTONDOWN:
	  app_on_joybdown(app, evt);
	  break;
	case SDL_JOYBUTTONUP:
	  app_on_joybup(app, evt);
	  break;
	case SDL_JOYDEVICEADDED:
	case SDL_JOYDEVICEREMOVED:
	  app_on_joydev(app, evt);
	  break;
	default:
	  break;
	}
    }

  app_cycle_gfx(app);
  return 0;
}

int app_main (app_t * app)
{
  app->alive = 1;
  int n = 0;
  int lasttick = 0;
  const int divisor = 500;
  while (app->alive != 0)
    {
      if ((n % divisor) == 0)
	{
	  int k = (n / divisor);
	  char buf[64];
	  int delta = SDL_GetTicks() - lasttick;
	  SDL_snprintf(buf, sizeof(buf), "Tick %d (+%d)", k, delta);
	  app_write(app, CAT_MISC, buf);
	  lasttick = SDL_GetTicks();
	}
      n++;

      app_cycle(app);
    }
  return 0;
}


app_t _app, *app=&_app;

int main (int argc, const char *argv[])
{
  app_init(app);
  app_main(app);
  app_destroy(app);
  return 0;
}

