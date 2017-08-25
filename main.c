/*
  wmvolt - A dockapp to monitor Linux ACPI battery status

  Copyright (c) 2017 Alexander Gromnitsky
  https://github.com/gromnitsky/wmvolt

  This a fork of wmapmload 0.3.4. The original copyright message:

  WMApmLoad - A dockapp to monitor APM status
  Copyright (C) 2002  Thomas Nemeth <tnemeth@free.fr>
  Based on work by Seiichi SATO <ssato@sh.rim.or.jp>
  Copyright (C) 2001,2002  Seiichi SATO <ssato@sh.rim.or.jp>
  and on work by Mark Staggs <me@markstaggs.net>
  Copyright (C) 2002  Mark Staggs <me@markstaggs.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define PACKAGE "wmvolt"
const char *argp_program_version = "0.0.1";

#define _XOPEN_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <err.h>
#include <argp.h>
#include "dockapp.h"
#include "backlight_on.xpm"
#include "backlight_off.xpm"
#include "parts.xpm"
#include "battery.h"

#define SIZE	    58
#define WINDOWED_BG ". c #AEAAAE"

Pixmap pixmap;
Pixmap backdrop_on;
Pixmap backdrop_off;
Pixmap parts;
Pixmap mask;
static unsigned switch_authorized = True;

typedef enum { LIGHTOFF, LIGHTON } Light;

typedef struct Conf {
  char *display;
  Light backlight;
  char *light_color;		// #rgb
  int update_interval;		// sec
  int alarm_level;		// %
  char *cmd_notify;
  char *cmd_suspend;
  char *cmd_hibernate;
  int battery;
  int verbose;
  char *debug_uevent;		// a file name
} Conf;

Conf conf = {
  .display = "",
  .backlight = LIGHTOFF,
  .light_color = NULL,
  .update_interval = 1,
  .alarm_level = 20,
  .cmd_notify = NULL,
  .cmd_suspend = "echo systemctl suspend",
  .cmd_hibernate = "echo systemctl hibernate",
  .battery = -1,
  .verbose = 0
};

/* prototypes */
static void gui_update(Battery*);
static void switch_light(Battery*);
static void draw_timedigit(Battery);
static void draw_pcdigit(Battery);
static void draw_statusdigit(Battery);
static void draw_pcgraph(Battery);
static void cl_parse(int, char **);
static void battery_set_current();
static void bt_update(Battery*);



int main(int argc, char **argv) {
  XEvent   event;
  XpmColorSymbol colors[2] = { {"Back0", NULL, 0}, {"Back1", NULL, 0} };
  int      ncolor = 0;
  struct   sigaction sa;

  sa.sa_handler = SIG_IGN;
#ifdef SA_NOCLDWAIT
  sa.sa_flags = SA_NOCLDWAIT;
#else
  sa.sa_flags = 0;
#endif
  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, NULL);

  cl_parse(argc, argv);

  /* Initialize Application */
  battery_set_current();
  Battery bt_current;
  bt_update(&bt_current);

  dockapp_open_window(conf.display, PACKAGE, SIZE, SIZE, argc, argv);
  dockapp_set_eventmask(ButtonPressMask);

  if (conf.light_color) {
    colors[0].pixel = dockapp_getcolor(conf.light_color);
    colors[1].pixel = dockapp_blendedcolor(conf.light_color, -24, -24, -24, 1.0);
    ncolor = 2;
  }

  /* change raw xpm data to pixmap */
  if (dockapp_iswindowed)
    backlight_on_xpm[1] = backlight_off_xpm[1] = WINDOWED_BG;

  if (!dockapp_xpm2pixmap(backlight_on_xpm, &backdrop_on, &mask, colors, ncolor))
    err(1, "error initializing backlit bg image");
  if (!dockapp_xpm2pixmap(backlight_off_xpm, &backdrop_off, NULL, NULL, 0))
    err(1, "error initializing bg image");
  if (!dockapp_xpm2pixmap(parts_xpm, &parts, NULL, colors, ncolor))
    err(1, "error initializing parts image");

  /* shape window */
  if (!dockapp_iswindowed) dockapp_setshape(mask, 0, 0);
  if (mask) XFreePixmap(display, mask);

  /* pixmap : draw area */
  pixmap = dockapp_XCreatePixmap(SIZE, SIZE);

  /* Initialize pixmap */
  if (conf.backlight == LIGHTON)
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, SIZE, SIZE, 0, 0);
  else
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, SIZE, SIZE, 0, 0);

  dockapp_set_background(pixmap);
  dockapp_show();

  /* Main loop */
  while (1) {
    if (dockapp_nextevent_or_timeout(&event, conf.update_interval * 1000)) {
      /* Next Event */
      switch (event.type) {
      case ButtonPress:
	switch (event.xbutton.button) {
	case 1: switch_light(&bt_current); break;
	case 2:
	  system(event.xbutton.state == ControlMask ? conf.cmd_hibernate: conf.cmd_suspend);
	  break;
	case 3: switch_authorized = !switch_authorized; break;
	default: break;
	}
	break;
      default: break;
      }
    } else {
      /* Time Out */
      gui_update(&bt_current);
    }
  }

  return 0;
}



static
void draw_all_the_digits(Battery bt) {
  draw_timedigit(bt);
  draw_pcdigit(bt);
  draw_statusdigit(bt);
  draw_pcgraph(bt);

  dockapp_copy2window(pixmap); // show
}

/* called by timer */
static
void gui_update(Battery *bt_current) {
  static Light pre_backlight;
  static Bool in_alarm_mode = False;

  bt_update(bt_current);

  /* alarm mode */
  if (bt_current->capacity < conf.alarm_level) {
    if (!in_alarm_mode) {
      in_alarm_mode = True;
      pre_backlight = conf.backlight;
      system(conf.cmd_notify);
    }
    if (switch_authorized ||
	(!switch_authorized && conf.backlight != pre_backlight)) {
      switch_light(bt_current);
      return;
    }
  } else {
    if (in_alarm_mode) {
      in_alarm_mode = False;
      if (conf.backlight != pre_backlight) {
	switch_light(bt_current);
	return;
      }
    }
  }

  /* all clear */
  if (conf.backlight == LIGHTON)
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, SIZE, SIZE, 0, 0);
  else
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, SIZE, SIZE, 0, 0);

  draw_all_the_digits(*bt_current);
}

/* called when mouse button pressed */
static
void switch_light(Battery *bt_current) {
  if (conf.backlight == LIGHTOFF) {
    conf.backlight = LIGHTON;
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, SIZE, SIZE, 0, 0);
  } else {
    conf.backlight = LIGHTOFF;
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, SIZE, SIZE, 0, 0);
  }

  draw_all_the_digits(*bt_current);
}

static void draw_timedigit(Battery infos) {
  int y = 0;
  int hour_left, min_left;

  if (conf.backlight == LIGHTON) y = 20;

  hour_left = infos.seconds_remaining / 3600;
  min_left = infos.seconds_remaining / 60 % 60;
  dockapp_copyarea(parts, pixmap, (hour_left / 10) * 10, y, 10, 20,  5, 7);
  dockapp_copyarea(parts, pixmap, (hour_left % 10) * 10, y, 10, 20, 17, 7);
  dockapp_copyarea(parts, pixmap, (min_left / 10)  * 10, y, 10, 20, 32, 7);
  dockapp_copyarea(parts, pixmap, (min_left % 10)  * 10, y, 10, 20, 44, 7);
}

static void draw_pcdigit(Battery infos) {
  int v100, v10, v1;
  int xd = 0;
  int num = infos.capacity;

  if (num < 0)  num = 0;

  v100 = num / 100;
  v10  = (num - v100 * 100) / 10;
  v1   = (num - v100 * 100 - v10 * 10);

  if (conf.backlight == LIGHTON) xd = 50;

  /* draw digit */
  dockapp_copyarea(parts, pixmap, v1 * 5 + xd, 40, 5, 9, 17, 45);
  if (v10 != 0)
    dockapp_copyarea(parts, pixmap, v10 * 5 + xd, 40, 5, 9, 11, 45);
  if (v100 == 1) {
    dockapp_copyarea(parts, pixmap, 5 + xd, 40, 5, 9, 5, 45);
    dockapp_copyarea(parts, pixmap, 0 + xd, 40, 5, 9, 11, 45);
  }
}

static void draw_statusdigit(Battery infos) {
  int xd = 0;
  int y = 31;

  if (conf.backlight == LIGHTON) {
    y = 40;
    xd = 50;
  }

  if (infos.is_charging)
    dockapp_copyarea(parts, pixmap, 100, y, 4, 9, 41, 45);

  if (infos.is_ac_power)
    dockapp_copyarea(parts, pixmap, 0 + xd, 49, 5, 9, 34, 45);
  else
    dockapp_copyarea(parts, pixmap, 5 + xd, 49, 5, 9, 48, 45);
}

static void draw_pcgraph(Battery infos) {
  int xd = 100;
  int nb;
  int num = infos.capacity / 6.25 ;

  if (num < 0) num = 0;

  if (conf.backlight == LIGHTON) xd = 102;

  /* draw digit */
  for (nb = 0 ; nb < num ; nb++)
    dockapp_copyarea(parts, pixmap, xd, 0, 2, 9, 6 + nb * 3, 33);
}

static error_t
parse_opt(int key, char *arg, struct argp_state *state) {
  Conf *args = state->input;
  int *bt_list;

  switch (key) {
  case 'd': args->display = arg; break;
  case 'b': args->backlight = LIGHTON; break;
  case 'l': args->light_color = arg; break;
  case 'u':
    args->update_interval = atoi(arg);
    if (args->update_interval < 1) errx(1, "-u should be > 1");
    break;
  case 'a':
    args->alarm_level = atoi(arg);
    if (args->alarm_level < 1 || args->alarm_level > 99)
      errx(1, "-a valid range: [1-99]");
    break;
  case 'w': dockapp_iswindowed = True; break;
  case 'W': dockapp_isbrokenwm = True; break;
  case 'n': args->cmd_notify = arg; break;
  case 'S': args->cmd_suspend = arg; break;
  case 'H': args->cmd_hibernate = arg; break;
  case 'p':
    bt_list = battery_list();
    if (bt_list) {
      while (*bt_list != -1) printf("%d ", *bt_list++);
      printf("\n");
      exit(0);
    }
    errx(1, "no batteries detected");
    break;
  case 'B': args->battery = atoi(arg); break;
  case 'v': args->verbose++; break;
  case 300: args->debug_uevent = arg; break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static void
cl_parse(int argc, char **argv) {
  struct argp_option options[] = {
    {"backlight",       'b', 0,      0, "Turn on the back-light" },
    {"display",         'd', "str",  0, "X11 display to use" },
    {"light-color",     'l', "#rgb", 0, "A back-light color" },
    {"update-interval", 'u', "num",  0, "Seconds between the updates" },
    {"alarm-level",     'a', "%",    0, "A low battery level that raises the alarm" },
    {"windowed",        'w', 0,      0, "Run the app in the windowed mode" },
    {"broken-wm",       'W', 0,      0, "Activate the broken WM fix" },
    {"cmd-notify",      'n', "str",  0, "A command to launch when the alarm is on" },
    {"cmd-suspend",     'S', "str",  0, "A command to supend the machine" },
    {"cmd-hibernate",   'H', "str",  0, "A command to hibernate the machine " },
    {"print-batteries", 'p', 0,      0, "Print all the available batteries" },
    {"battery",         'B', "num",  0, "Explicitly select the battery" },
    {"verbose",         'v', 0,      0, "Increase the verbosity level" },
    {"debug-uevent",    300, "file", 0, "Use the fake uevent data" },
    { 0 }
  };
  struct argp argp = { options, parse_opt, NULL, NULL };
  argp_parse(&argp, argc, argv, 0, 0, &conf);
}

static
char *iso8601() {
  char *buf = malloc(21);
  time_t now = time(NULL);
  strftime(buf, 21, "%FT%TZ", gmtime(&now));
  return buf;
}

static
void bt_update(Battery *bt_current) {
  if (conf.verbose) {
    char *ts = iso8601();
    fprintf(stderr, "%s: bt_update(): ", ts);
    free(ts);
  }

  Battery bt;
  bool r = conf.debug_uevent ? battery_get_from_file(conf.debug_uevent, &bt) : battery_get(conf.battery, &bt);
  if (!r) err(1, "failed to get data for battery #%d", conf.battery);

  bt_current->is_ac_power = bt.is_ac_power;
  bt_current->is_charging = bt.is_charging;
  bt_current->capacity = bt.capacity;
  bt_current->seconds_remaining = bt.seconds_remaining;

  if (conf.verbose) {
    fprintf(stderr, "id=%d, ac=%d, charging=%d, %%=%d, sec=%d\n",
	    bt.id, bt.is_ac_power, bt.is_charging, bt.capacity,
	    bt.seconds_remaining);
  }
}

static
void battery_set_current() {
  if (conf.battery != -1) return;

  int *bt_list = battery_list();
  if (!bt_list) errx(1, "no batteries detected");
  conf.battery = bt_list[0];
  free(bt_list);
}
