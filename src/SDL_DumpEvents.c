/* gcc `pkg-config --cflags --libs sdl2 SDL2_ttf`

-Wall -Wextra -pedantic -Wstrict-prototypes
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
#ifndef HAVE_GETOPT_LONG
#define HAVE_GETOPT_LONG 1
#endif


#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

/* safe to comment out the following line. */
#include <unistd.h>

#include <SDL.h>
#include <SDL_ttf.h>

#define PACKAGE "SDL_DumpEvents"
#define VERSION "0.01"

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
#ifndef BUILDIN_TTF
#define BUILDIN_TTF DEFAULT_FONT_FILENAME
#endif /* BUILDIN_TTF */
#define DIR_SEPARATOR "/"

/* Maximum length of one log line, bytes. */
#define MAX_LINELENGTH 256
/* Maximum number of log lines per category. */ /* 2400p / 16perRow */
#define MAX_NUMLINES 150
/* Max number of joystick devices to recognize. */
#define MAX_JOYSTICKS 8
/* Max number of game controller (gamepad) devices to recognize. */
#define MAX_GAMEPADS 8
/* Max number of permanent graphics decorations supported. */
#define MAX_GFXDECOR 30
/* Max number of heartbeat data to store for reporting. */
#define MAX_HEARTBEATS 17
/* Number of mainloop cycles to advance heartbeat by one. */
#define MAINLOOP_PER_HEARTBEAT 500

/* Default window size. */
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

/* "adjustment" pixels vertically to reserve away from the scrolling log. */
#define RESERVED_ROWS 80

#define DEFAULT_MAPPING_ENVVAR "SDL_DUMPEVENTS_MAPPING"

/* Fade effect parameters. */
#define DEFAULT_AGE_FADE_PERIOD 1000
#define DEFAULT_AGE_FADE_ALPHA_START 0xff
#define DEFAULT_AGE_FADE_ALPHA_END 0x7f


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

/* Category labels. */
static const char * catlabel[MAX_CATEGORIES] = {
    "MISC",
    "KEYB",
    "MOUSE",
    "JOY",
    "SDL_CONTROLLER",
};


/* One log entry line. */
typedef struct logentry_s {
    char line[MAX_LINELENGTH];

    struct {
	long spawntime; /* spawn time, to calculate age. */
	SDL_bool active; /* renderer to update texture properties with... */
	unsigned char intensity;  /* alpha value. */
    } fade;

    SDL_Surface * surf;
    SDL_Texture * tex;
} logentry_t;

/* List of logentry instances; one per category column. */
typedef struct logbuf_s {
    int alloc; // number of lines allocated in memory (when using malloc).
    /* alloc == 0 if using the static memory space, which is also used if malloc()/realloc() returns NULL (i.e. malloc() can be stubbed as "return 0;"). */
    int cap;   // maximum lines permitted.
    int len;   // current lines valid.
    int head;  // ring buffer.

    logentry_t _static[MAX_NUMLINES];
    logentry_t * buf;
} logbuf_t;

enum mapping_protocol_e {
    MAPPING_NONE = 0,
    MAPPING_LITERAL,
    MAPPING_ENV,
    MAPPING_FILE,
    MAPPING_HELP
};

/* Persistent graphics elements. */
typedef struct gfxdecor_s {
    int x;
    int y;
    SDL_Surface * surf;
    SDL_Texture * tex;
} gfxdecor_t;

typedef struct app_s {
    int alive;

    int logginess;
    enum mapping_protocol_e mapping_protocol;
    const char * mapping_locator;
    long age_fade_period;
    int age_fade_start;
    int age_fade_end;
    SDL_bool log_heartbeat;

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

    int rowsize;

    SDL_RWops * font_io[1];  /* SDL_RWops* type for TTF (file). */
    TTF_Font * fonts[4];
    SDL_Joystick * jspack[MAX_JOYSTICKS];
    SDL_GameController * gcpack[MAX_GAMEPADS];

    /* Persistent decorations. */
    gfxdecor_t decor[MAX_GFXDECOR];

    /* A logbuf instance per column. */
    logbuf_t logbuf[MAX_CATEGORIES];

    /* Heartbeat samples. */
    struct heartbeats_s {
	int cycles_per_heartbeat; /* steps of mainloop per heartbeat */
	int t;  /* last seen timestamp. */
	int n;  /* number of steps since last heartbeat. */
	long samples[MAX_HEARTBEATS]; /* the samples. */
	int nsamples;  /* number of samples valid */
	int nextsample; /* indext to store next sample (circular buffer). */
	char report[60];  /* text to show as heartbeat report. */
    } heartbeats;

    /* SDL window title. */
    char title0[255];
} app_t;




#ifdef __GNUC__
/* This bit of assembly incorporates the contents of the default font file, yielding symbols 'ttf0_start', 'ttf0_end', 'ttf0_size'.
Values intended to mimick the effect of using "ld -r -b binary ..." or objcopy to include arbitrary binary file into object file.
*/
__asm__(
	".global _binary_ttf0_start\n.global _binary_ttf0_end\n.global _binary_ttf0_size\n"
	"_binary_ttf0_start: .incbin \"" BUILDIN_TTF "\"\n"
	"_binary_ttf0_end: .byte 0\n"
	".set _binary_ttf0_size, (_binary_ttf0_end - _binary_ttf0_start)\n"
);
extern const unsigned char _binary_ttf0_start[];
extern const unsigned char _binary_ttf0_end[];  /* no content; address significant. */
extern const struct{void*_;} _binary_ttf0_size;  /* no content; address significant */

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



const char BANNER[] = APP_TITLE " - add as Non-Steam Game, run from Big Picture Mode; ESCAPE to quit";


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
  if (cap <= MAX_NUMLINES)
    {
      /* use static space. */
      logbuf->alloc = 0;
      logbuf->buf = logbuf->_static;
    }
  else
    {
      /* use heap. */
      logbuf->alloc = sizeof(logentry_t) * cap;
      logbuf->buf = SDL_malloc(logbuf->alloc);
      if (! logbuf->buf)
	{
	  /* fallback to static. */
	  logbuf->alloc = 0;
	  logbuf->cap = MAX_NUMLINES;
	  logbuf->buf = logbuf->_static;
	}
      else
	{
	  logbuf->cap = cap;
	}
    }
  return logbuf;
}

logbuf_t * logbuf_destroy (logbuf_t * logbuf)
{
  return logbuf;
}

int logbuf_resize (logbuf_t * logbuf, int histlen)
{
  if (histlen <= MAX_NUMLINES)
    {
      /* use static space. */
      logbuf->alloc = 0;
      logbuf->cap = histlen;
      logbuf->buf = logbuf->_static;
    }
  else
    {
      /* use heap. */
      logbuf->alloc = sizeof(logentry_t) * histlen;
      logbuf->buf = SDL_realloc(logbuf->buf, logbuf->alloc);
      if (! logbuf->buf)
	{
	  /* fallback to static. */
	  logbuf->alloc = 0;
	  logbuf->cap = MAX_NUMLINES;
	  logbuf->buf = logbuf->_static;
	}
      else
	{
	  logbuf->cap = histlen;
	}
    }
  logbuf->head = 0;
  logbuf->len = 0;

  return logbuf->cap;
}

/* append line to buffer. */
int logbuf_append (logbuf_t * logbuf, const char * buf, int buflen)
{
  int n = (logbuf->head + logbuf->len) % logbuf->cap;
  SDL_memcpy(logbuf->buf[n].line, buf, buflen);
  logbuf->buf[n].fade.spawntime = SDL_GetTicks();
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
  return 0;
}

int logbuf_len (logbuf_t * logbuf)
{
  return logbuf->len;
}

int logbuf_clear (logbuf_t * logbuf)
{
  logbuf->head = 0;
  logbuf->len = 0;
  return 0;
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


int logbuf_test (void)
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
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "DUMP:");
  for (i = 0; i < 8; i++)
    {
      const logentry_t * entry = logbuf_get(logbuf, i);
      const char * line = entry->line;
      if (line)
	{
	  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, " %d: %s", i, line);
	}
    }
  return 0;
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



const char * version_summary = \
PACKAGE " " VERSION "\n"
"Copyright 2018 Fred Lee <fredslee27@gmail.com>\n"
"License: GPLv3+ (GNU General Public License version 3 or later)\n"
;

#if HAVE_GETOPT_LONG
const char * options_summary = \
"  -h, --help                Show this help screen and quit.\n"
"  -V, --version             Show version information and quit.\n"
"  -v N, --verbose=N         Set logging verbosity [0].\n"
"  -r WxH, --resolution=WxH  Set window resolution [1280x720].\n"
"  -m FILE, --map-file=FILE  Load SDL game controller mappings from file.\n"
"  -M MAP, --map-string=MAP  Add SDL game controller mapping.\n"
"  --map-env=ENVNAME         SDL game controller mapping from environment.\n"
"  --map-help                Dump controller GUIDs and names.\n"
"\n"
"Mapping information at https://wiki.libsdl.org/SDL_GameControllerAddMapping\n"
;

const char * OPT_HELP = "help";
const char * OPT_VERSION = "version";
const char * OPT_VERBOSE = "verbose";
const char * OPT_RESOLUTION = "resolution";
const char * OPT_MAPPING = "mapping";
const char * OPT_MAP_FILE = "map-file";
const char * OPT_MAP_ENV = "map-env";
const char * OPT_MAP_STRING = "map-string";
const char * OPT_MAP_HELP = "map-help";

app_t * app_parse_argv (app_t * app, int argc, char ** argv)
{
  int show_usage = 0;
  int show_version = 0;
  int mapping_action = 0;
  static const char * optstring = "h?Vvr:m:M:";
  const struct option longopts[] = {
      /* { long_name:string, has_arg:int, flag:address, val:int } */
	{ OPT_HELP, no_argument, NULL, 'h' },
	{ OPT_VERSION, no_argument, NULL, 'V' },
	{ OPT_VERBOSE, required_argument, NULL, 'v' },
	{ OPT_RESOLUTION, required_argument, NULL, 'r' },
	{ OPT_MAPPING, required_argument, &mapping_action, 1 },
	{ OPT_MAP_FILE, optional_argument, NULL, 'm' },
	{ OPT_MAP_ENV, optional_argument, NULL, 0 },
	{ OPT_MAP_STRING, required_argument, NULL, 'M' },
	{ OPT_MAP_HELP, no_argument, NULL, 0 },
	{ 0, 0, 0, 0 }
  };

  int optval = 0;
  int longindex = 0;
  while (((optval = getopt_long(argc, argv, optstring, longopts, &longindex))) != -1)
    {
      /* optarg is the string value for optional/required arguments. */
      switch (optval)
	{
	case '?':
	case 'h': /* show help */
	  show_usage = 1;
	  break;
	case 'V': /* show version */
	  show_version = 1;
	  break;
	case 'v': /* set verbosity */
	  app->logginess = SDL_atoi(optarg);
	  break;
	case 'm': /* set mapping file */
	  app->mapping_protocol = MAPPING_FILE;
	  app->mapping_locator = optarg ? optarg : "";
	  break;
	case 'M': /* set mapping string */
	  app->mapping_protocol = MAPPING_LITERAL;
	  app->mapping_locator = optarg;
	  break;
	case 'r': /* set resolution */
	    {
	      char * endptr;
	      app->width = SDL_strtol(optarg, &endptr, 0);
	      if (*endptr)
		{
		  endptr++;
		  if (*endptr)
		    {
		      app->height = SDL_strtol(endptr, &endptr, 0);
		    }
		}
	    }
	  break;
	case 0: /* long-only option. */
	  if (longopts[longindex].name == OPT_MAP_HELP)
	    {
	      app->mapping_protocol = MAPPING_HELP;
	    }
	  else if (longopts[longindex].name == OPT_MAP_ENV)
	    {
	      app->mapping_protocol = MAPPING_ENV;
	      app->mapping_locator = optarg ? optarg : DEFAULT_MAPPING_ENVVAR;
	    }
	  break;
	}
    }
  if (show_usage)
    {
      printf("usage: %s [OPTIONS]\n\n", argv[0]);
      puts(options_summary);
      return NULL;
    }
  if (show_version)
    {
      puts(version_summary);
      return NULL;
    }

  return app;
}
#else
/* No GNU getopt_long. */

app_t * app_parse_argv (app_t * app, int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  return app;
}
#endif


/* utility function: pick first index in array of pointers fulfilling a criteria (default is-NULL, for finding an empty slot). */
static
int find_slot (int packcount, void ** pack, int (*predicate)(void*, void*), void * userdata)
{
  int i = 0;
  for (i = 0; i < packcount; i++)
    {
      if (predicate)
	{
	  /* delegate to predicate function. */
	  if (predicate(pack[i], userdata))
	    break;
	}
      else
	{
	  /* default predicate: compare to NULL */
	  if (pack[i] == NULL)
	    break;
	}
    }
  if (i < packcount)
    return i;
  return -1;
}



/*
   Initialize app state.

   Returns pointer to app instance,
   or returns NULL if app cannot proceed (error or forced quit).
*/
app_t * app_init (app_t * app, int argc, char ** argv)
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
  /* Zero out struct. */
  SDL_memset(app, 0, sizeof(*app));

  int i;
  for (i = 0; i < MAX_CATEGORIES; i++)
    {
      logbuf_init(app->logbuf + i, 0);
    }

  /* Parse command-line arguments here. */
  if (! app_parse_argv(app, argc, argv))
    return NULL;


  /* Start invoking SDL. */
  SDL_Init(SDL_INIT_EVERYTHING);


  /* Prepare game controller mappings. */
  switch (app->mapping_protocol)
    {
    case MAPPING_FILE:
      if (app->mapping_locator[0])
	{
	  /* filename */
	  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
		      "Loading mapping from '%s'",
		      app->mapping_locator);
	  int res = SDL_GameControllerAddMappingsFromFile(app->mapping_locator);
	  if (res == -1)
	    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Error loading mappings from '%s'.", app->mapping_locator);
	}
      else
	{
	  /* stdin */
	  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
		      "Loading mapping from stdin");
	  SDL_RWops * io = SDL_RWFromFP(stdin, SDL_TRUE);
	  SDL_GameControllerAddMappingsFromRW(io, SDL_TRUE);
	}
      break;
    case MAPPING_ENV:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
		  "Loading mapping from env '%s'",
		  app->mapping_locator);
      SDL_GameControllerAddMapping(getenv(app->mapping_locator));
      break;
    case MAPPING_LITERAL:
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
		  "Loading mapping \"%s\".",
		  app->mapping_locator);
      SDL_GameControllerAddMapping(getenv(app->mapping_locator));
      break;
    case MAPPING_HELP:
	{
	  int i;
	  char jsguid[80] = { 0, };
	  char jsname[80] = { 0, };
	  for (i = 0; i < SDL_NumJoysticks(); i++)
	    {
	      jsguid[0] = 0;
	      jsname[0] = 0;
	      SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
	      SDL_JoystickGetGUIDString(guid, jsguid, sizeof(jsguid));
	      SDL_snprintf(jsname, sizeof(jsname), "%s", SDL_JoystickNameForIndex(i));
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "JS #%d: GUID=%s (%s)\n", i, jsguid, jsname);
	    }
	  return NULL;
	}
      break;
    case MAPPING_NONE:
    default:
      break;
    }

  /* Enable joystick events. */
  SDL_JoystickEventState(SDL_ENABLE);
  /* Enable game controller events. */
  SDL_GameControllerEventState(SDL_ENABLE);

  /* open main window. */
  SDL_snprintf(app->title0, sizeof(app->title0), APP_TITLE);
  if (! app->width) app->width = DEFAULT_WIDTH;
  if (! app->height) app->height = DEFAULT_HEIGHT;
  app->x0 = SDL_WINDOWPOS_UNDEFINED;
  app->y0 = SDL_WINDOWPOS_UNDEFINED;
  app->wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  app->rindex = 0;
  app->rflags = 0;
  app->rowsize = 20;

  app->w = SDL_CreateWindow(app->title0,
			    app->x0, app->y0,
			    app->width, app->height,
			    app->wflags);
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Opened window %dx%d", app->width, app->height);
  app->r = SDL_CreateRenderer(app->w, -1, app->rflags);

  if (! app->age_fade_period) app->age_fade_period = DEFAULT_AGE_FADE_PERIOD;
  if (! app->age_fade_start) app->age_fade_start = DEFAULT_AGE_FADE_ALPHA_START;
  if (! app->age_fade_end) app->age_fade_end = DEFAULT_AGE_FADE_ALPHA_END;


  /* load fonts. */
  TTF_Init();

  if (ttf0_size > 0)
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


  /* Open game controllers (apply mappings) */
  for (int jsnum = 0; jsnum < SDL_NumJoysticks(); jsnum++)
    {
      if (SDL_IsGameController(jsnum))
	{
	  /* Emulate controller device add event to trigger handler. */
	  SDL_Event pushevt;
	  pushevt.cdevice.type = SDL_CONTROLLERDEVICEADDED;
	  pushevt.cdevice.timestamp = 0;
	  pushevt.cdevice.which = jsnum;
	  SDL_PushEvent(&pushevt);
	}
    }


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

int app_clear (app_t * app)
{
  for (int catnum = 0; catnum < MAX_CATEGORIES; catnum++)
    {
      logbuf_clear(app->logbuf + catnum);
    }
  return 0;
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
  (void)evt;  /* deliberately ignoring argument. */
  app_write(app, CAT_MISC, "QUIT");
  app->alive = 0;
  return 0;
}

int app_invalidate_decors (app_t * app);

int app_resize (app_t * app, int width, int height)
{
  app->width = width;
  app->height = height;
  app_clear(app);
  app_invalidate_decors(app);
  int histsize = (height - RESERVED_ROWS) / app->rowsize;
  if (histsize < 1) histsize = 1;
  for (int catnum = 0; catnum < MAX_CATEGORIES; catnum++)
    {
      logbuf_resize(app->logbuf + catnum, histsize);
    }

  return 0;
}

int app_on_window (app_t * app, SDL_Event * evt)
{
  switch (evt->window.event)
    {
    case SDL_WINDOWEVENT_SHOWN:
      app_write(app, CAT_MISC, "WIN SHOWN");
      break;
    case SDL_WINDOWEVENT_HIDDEN:
      app_write(app, CAT_MISC, "WIN HIDDEN");
      break;
    case SDL_WINDOWEVENT_EXPOSED:
      app_write(app, CAT_MISC, "WIN EXPOSED");
      break;
    case SDL_WINDOWEVENT_MOVED:
      app_write(app, CAT_MISC, "WIN MOVED");
      break;
    case SDL_WINDOWEVENT_RESIZED:
      app_write(app, CAT_MISC, "WIN RESIZED");
      break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
      /* TODO: check windowID for which window. */
      app_resize(app, evt->window.data1, evt->window.data2);
      app_write(app, CAT_MISC, "WIN SIZE_CHANGED");
      break;
    case SDL_WINDOWEVENT_MINIMIZED:
      app_write(app, CAT_MISC, "WIN MINIMIZED");
      break;
    case SDL_WINDOWEVENT_MAXIMIZED:
      app_write(app, CAT_MISC, "WIN MAXIMIZED");
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
      app_write(app, CAT_MISC, "WIN FOCUS_GAINED");
      break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
      app_write(app, CAT_MISC, "WIN FOCUS_LOST");
      break;
    case SDL_WINDOWEVENT_CLOSE:
      app_write(app, CAT_MISC, "WIN CLOSE");
      break;
    default:
      break;
    }
  return 0;
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
  char jsguid[80] = { 0, };
  int n = 0;
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
      packidx = find_slot(MAX_JOYSTICKS, (void**)(app->jspack), NULL, NULL);
      if ((0 <= packidx) && (packidx < MAX_JOYSTICKS))
	{
	  /* valid slot, open device. */
	  SDL_Joystick * openjs = SDL_JoystickOpen(devnum);
	  app->jspack[packidx] = openjs;
	  if (openjs)
	    {
	      instid = SDL_JoystickInstanceID(openjs);
	      SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(openjs), jsguid, sizeof(jsguid));
	      n = SDL_snprintf(jsname, sizeof(jsname), "%s", SDL_JoystickName(openjs));
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Opened joystick handle %d as instance #%d from index %d \"%s\" (%s).", packidx, instid, devnum, jsname, jsguid);
	    }
	  else
	    {
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Out of handles while tyring to open joystick #%d", devnum);
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
	      n = SDL_snprintf(jsname, sizeof(jsname), "%s", SDL_JoystickName(doomedjs));
	      SDL_JoystickClose(app->jspack[packidx]);
	      app->jspack[packidx] = NULL;
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Closed joystck handle %d being joystick #%d \"%s\"", packidx, instid, jsname);
	    }
	}
      break;
    }
  /* TODO: truncate jsname at 12th glyph, not 12th byte. */
  if (n > 11)
    jsname[11] = '\0';
  app_fwrite(app, CAT_JOY, "%s: %d=%s", action, instid, jsname);
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

static
int gamedev_instanceid_eq_p (void * obj, void * userdata)
{
  SDL_GameController * gc = (SDL_GameController*)obj;
  int cmp = (long)userdata;
  if (obj == NULL) return 0;
  return (cmp == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)));
}

/* handle SDL Game Controller device events: add, remove, remap. */
int app_on_gamedev (app_t * app, SDL_Event * evt)
{
  const char * action = "?";
  char gcname[80] = { 0, };
  int n = 0;
  int devnum = -1;
  long instid = -1;
  int packidx = -1;

  switch (evt->cdevice.type)
    {
    case SDL_CONTROLLERDEVICEADDED:
      action = "ADD";
      /* avoid duplicate */
      instid = evt->cdevice.which;
      packidx = find_slot(MAX_GAMEPADS, (void**)(app->gcpack),
			  gamedev_instanceid_eq_p, (void*)instid);
      if (packidx != -1) /* controller already handled. */
	{
	  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Re-opening game controller (handle=%d, jsinstance=%ld)\n", packidx, instid);
	  action = NULL;
	  break;
	}
      /* find a slot. */
      packidx = find_slot(MAX_GAMEPADS, (void**)(app->gcpack), NULL, NULL);
      if (packidx > -1)
	{
	  /* found slot; open device. */
	  devnum = evt->cdevice.which;
	  SDL_GameController * opengc = SDL_GameControllerOpen(devnum);
	  if (opengc)
	    {
	      n = SDL_snprintf(gcname, sizeof(gcname), "%s", SDL_GameControllerName(opengc));
	      instid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(opengc));
	      app->gcpack[packidx] = opengc;
	      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Opened game controller (handle=%d, jsinstance=%ld, sysid=%d) \"%s\".", packidx, instid, devnum, gcname);
	    }
	  else
	    {
	      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to open game controller #%d.n", devnum);
	    }
	}
      else
	{
	  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Out of handles trying to open game controller #%d.", devnum);
	}

      break;
    case SDL_CONTROLLERDEVICEREMOVED:
      action = "REMOVE";
      instid = evt->cdevice.which;
      packidx = find_slot(MAX_GAMEPADS, (void**)(app->gcpack),
			  gamedev_instanceid_eq_p, (void*)instid);
      if (packidx > -1)
	{
	  /* found for removal. */
	  SDL_GameController * gc = app->gcpack[packidx];
	  instid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
	  n = SDL_snprintf(gcname, sizeof(gcname), "%s", SDL_GameControllerName(gc));
	  SDL_GameControllerClose(gc);
	  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Closed game controller %d (js #%ld) \"%s\".", packidx, instid, gcname);
	  app->gcpack[packidx] = NULL;
	}
      break;
    case SDL_CONTROLLERDEVICEREMAPPED:
      action = "REMAP";
      instid = evt->cdevice.which;
      packidx = find_slot(MAX_GAMEPADS, (void**)(app->gcpack),
			  gamedev_instanceid_eq_p, (void*)instid);
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Remapping on game controller %d.", packidx);
      break;
    }
  /* TODO: truncate gcname at 12th glyph, not 12th byte. */
  if (action)
    {
      if (n > 11)
	gcname[11] = '\0';
      app_fwrite(app, CAT_CONTROLLER, "%s: %ld=%s",
		 action,
		 instid,
		 gcname);
    }
  return 0;
}



/* Render text at a location for the current presentation cycle (frame). */
int app_printxy (app_t * app, TTF_Font * fon, int x, int y, const char * msg)
{
  SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
  SDL_Surface * textsurf = TTF_RenderUTF8_Blended(fon, msg, fg);
  SDL_Rect dst = { x, y, textsurf->w, textsurf->h };
  SDL_Texture * blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
  SDL_RenderCopy(app->r, blttex, NULL, &dst);

  SDL_DestroyTexture(blttex);
  SDL_FreeSurface(textsurf);

  return 0;
}

struct gfxdecor_s * app_get_decor (app_t * app, int decor_idx)
{
  if ((decor_idx < 0) || (decor_idx >= MAX_GFXDECOR))
    return NULL;
  struct gfxdecor_s * retval = app->decor + decor_idx;
  if ((! retval->surf) && (! retval->tex))
    return NULL;
  return retval;
}

int app_invalidate_decors (app_t * app)
{
  for (int decor_idx = 1; decor_idx <= MAX_CATEGORIES; decor_idx++)
    {
      struct gfxdecor_s * retval = app->decor + decor_idx;
      if (retval->tex)
	{
	  SDL_DestroyTexture(retval->tex);
	  retval->tex = NULL;
	}
      if (retval->surf)
	{
	  SDL_FreeSurface(retval->surf);
	  retval->surf = NULL;
	}
    }
  return 0;
}

/* Generate and store text at a location to be rendered across many presentation cycles. */
int app_install_text (app_t * app, int decor_idx, TTF_Font * fon, int x, int y, const char * msg)
{
  SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
  SDL_Texture * blttex = NULL;
  SDL_Surface * textsurf = app->decor[decor_idx].surf;

  if (textsurf)
    {
      /* delete in preparation for overwrite. */
      SDL_FreeSurface(textsurf);
      textsurf = NULL;
      blttex = app->decor[decor_idx].tex;
      if (blttex)
	{
	  SDL_DestroyTexture(blttex);
	  app->decor[decor_idx].tex = NULL;
	}
    }

  textsurf = TTF_RenderUTF8_Blended(fon, msg, fg);
  blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
  app->decor[decor_idx].x = x;
  app->decor[decor_idx].y = y;
  app->decor[decor_idx].surf = textsurf;
  app->decor[decor_idx].tex = blttex;

  return 0;
}

/* Render the permanent decorations. */
int app_render_decor (app_t * app)
{
  for (int i = 0; i < MAX_GFXDECOR; i++)
    {
      struct gfxdecor_s * decor = app->decor + i;
      SDL_Texture * blttex = decor->tex;
      SDL_Surface * textsurf = decor->surf;
      if (! blttex)
	{
	  textsurf = decor->surf;
	  if (!textsurf)
	    continue;
	  blttex = SDL_CreateTextureFromSurface(app->r, textsurf);
	  decor->tex = blttex;
	}
      SDL_Rect dst;
      dst.x = decor->x;
      dst.y = decor->y;
      dst.w = textsurf->w;
      dst.h = textsurf->h;

      SDL_RenderCopy(app->r, blttex, NULL, &dst);
    }

  return 0;
}

/* Handle all graphics output. */
int app_cycle_gfx (app_t * app, long t)
{
  (void)t;
  SDL_SetRenderDrawColor(app->r, 0,0,0,0);
  SDL_RenderClear(app->r);

  SDL_SetRenderDrawColor(app->r, 0xff,0xff,0xff,0xff);

  int x0 = 0;
  int y0 = 40;
  int x, y;
  for (int catnum = 0; catnum < MAX_CATEGORIES; catnum++)
    {
      x = x0 + (catnum * app->width / MAX_CATEGORIES);
      y = y0;
      if (catnum > 0)
	{
	  // separator line.
	  SDL_RenderDrawLine(app->r, x-4, y, x-4, app->height);
	}

      /* Place category name as column header. */
      if (!app_get_decor(app, catnum+1))
	app_install_text(app, catnum+1, app->fonts[2], x, y, catlabel[catnum]);

      /* Render log lines for current category. */
      struct logbuf_s * logbuf = app->logbuf + catnum;
      int maxlines = logbuf->cap;
      for (int linenum = 0; linenum < maxlines; linenum++)
	{
	  y += app->rowsize;
	  logentry_t * entry = logbuf_get(app->logbuf + catnum, linenum);
	  if (!entry) continue;
	  SDL_Surface * textsurf = entry->surf;
	  SDL_Texture * blttex = entry->tex;
	  if (blttex)
	    {
	      /* age-fade effect. */
	      if (entry->fade.active)
		{
		  SDL_SetTextureAlphaMod(blttex, entry->fade.intensity);
		}

	      /* render destination. */
	      SDL_Rect dst;
	      dst.x = x;
	      dst.y = y;
	      if (textsurf)
		{
		  dst.w = textsurf->w;
		  dst.h = textsurf->h;
		}
	      else
		{
		  Uint32 fmt = 0;
		  int access = 0;
		  SDL_QueryTexture(blttex, &fmt, &access, &dst.w, &dst.h);
		}

	      /* render. */
	      SDL_RenderCopy(app->r, blttex, NULL, &dst);
	    }
	}

      /* render the permanent decorations. */
      app_render_decor(app);

    }

  SDL_RenderPresent(app->r);

  return 0;
}

int app_cycle_events (app_t * app)
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
	case SDL_CONTROLLERAXISMOTION:
	  app_on_gameaxis(app, evt);
	  break;
	case SDL_CONTROLLERBUTTONDOWN:
	  app_on_gamebdown(app, evt);
	  break;
	case SDL_CONTROLLERBUTTONUP:
	  app_on_gamebup(app, evt);
	  break;
	case SDL_CONTROLLERDEVICEADDED:
	case SDL_CONTROLLERDEVICEREMOVED:
	case SDL_CONTROLLERDEVICEREMAPPED:
	  app_on_gamedev(app, evt);
	  break;
	default:
	  break;
	}
    }

  return 0;
}

int app_cycle_updates (app_t * app, long t)
{
  /* update heartbeat history. */
  struct heartbeats_s * heartbeats = &(app->heartbeats);
  if (0 == heartbeats->cycles_per_heartbeat)
    {
      heartbeats->cycles_per_heartbeat = MAINLOOP_PER_HEARTBEAT;
    }
  const int divisor = heartbeats->cycles_per_heartbeat;

  if ((heartbeats->n % divisor) == 0)
    {
      int k = (heartbeats->n / divisor);
      char buf[64];
      int delta = t - heartbeats->t;
      if (app->log_heartbeat)
	{
	  SDL_snprintf(buf, sizeof(buf), "Tick %d (+%d)", k, delta);
	  app_write(app, CAT_MISC, buf);
	}
      heartbeats->t = t;

      /* nextsample wraps in circular buffer, nsamples ceilings at max. */
      heartbeats->samples[heartbeats->nextsample++] = delta;
      heartbeats->nextsample %= MAX_HEARTBEATS;
      if (heartbeats->nsamples < MAX_HEARTBEATS)
	heartbeats->nsamples++;

      /* calculate various statistical values. */
      long sum = 0;
      long sumsq = 0;
      long nsamples = heartbeats->nsamples;
      for (int i = 0; i < heartbeats->nsamples; i++)
	{
	  long sample = heartbeats->samples[i];
	  sum += sample;
	  sumsq += (sample * sample);
	}

      long variance = 0;
      long mean = delta;
      if (nsamples > 1)
	{
	  variance = (sumsq - (sum*sum / nsamples)) / (nsamples-1);
	  mean = sum / nsamples;
	}
      long sigma = SDL_sqrtf(variance);

      static const char heart0[] = "♥";
      static const char heart1[] = "♡";
      int which = k % 2;
      SDL_snprintf(buf, sizeof(buf), "%s +%d x̄=%ld σ=%ld", which ? heart0 : heart1, delta, mean, sigma);
      int x = 0;
      int y = app->height - 20;
      app_install_text(app, MAX_GFXDECOR-1, app->fonts[2], x, y, buf);
    };
  heartbeats->n++;

  /* banner text at top of surface. */
  if (!app_get_decor(app, 0))
    app_install_text(app, 0, app->fonts[2], 0, 0, BANNER);


  for (int catnum = 0; catnum < MAX_CATEGORIES; catnum++)
    {
      struct logbuf_s * logbuf = app->logbuf + catnum;
      int maxlines = logbuf->cap;
      for (int linenum = 0; linenum < maxlines; linenum++)
	{
	  /* Synchronize associated text surface. */
	  logentry_t * entry = logbuf_get(logbuf, linenum);
	  if (!entry) continue;
	  if (! entry->surf)
	    {
	      SDL_Color fg = { 0xff, 0xff, 0xff, 0xff };
	      TTF_Font * fon = app->fonts[2];
	      const char * msg = entry ? entry->line : NULL;
	      if (msg && *msg)
		{
		  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "create text %d,%d", catnum, linenum);
		  SDL_Surface * textsurf = TTF_RenderUTF8_Blended(fon, msg, fg);
		  entry->tex = SDL_CreateTextureFromSurface(app->r, textsurf);
		  entry->surf = textsurf;
		}
	    }

	  /* Calculate age-fade effect. */
	  long age = t - entry->fade.spawntime;
	  if (age < app->age_fade_period)
	    {
	      /* calculate fading effect. */
	      int age_scaled = (app->age_fade_start - app->age_fade_end) * age / app->age_fade_period;
	      entry->fade.intensity = app->age_fade_start - age_scaled;
	      entry->fade.active = SDL_TRUE;
	    }
	  else if (age < app->age_fade_period * 2)
	    {
	      /* clamp at end effect for a while. */
	      entry->fade.intensity = app->age_fade_end;
	      entry->fade.active = SDL_TRUE;
	    }
	  else if (entry->fade.active)
	    {
	      entry->fade.active = SDL_FALSE;
	    }
	}
    }

  return 0;
}

/* One step of main loop. */
int app_cycle (app_t * app, long t)
{
  app_cycle_events(app);

  app_cycle_updates(app, t);

  app_cycle_gfx(app, t);
  return 0;
}

int app_main (app_t * app)
{
  app->alive = 1;

  /* main loop */
  while (app->alive != 0)
    {
      long t = SDL_GetTicks();
      app_cycle(app, t);
    }

  return 0;
}


app_t _app, *app=&_app;

int main (int argc, char *argv[])
{
  if (!app_init(app, argc, argv))
    return 1;
  app_main(app);
  app_destroy(app);
  return 0;
}

