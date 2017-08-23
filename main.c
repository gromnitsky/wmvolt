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
#define VERSION "1.0.0"

#define _XOPEN_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include "dockapp.h"
#include "backlight_on.xpm"
#include "backlight_off.xpm"
#include "parts.xpm"
#include "battery.h"

#define SIZE	    58
#define WINDOWED_BG ". c #AEAAAE"

typedef struct ApmInfos {
  const char driver_version[10];
  int        apm_version_major;
  int        apm_version_minor;
  int        apm_flags;
  int        ac_line_status;
  int        battery_status;
  int        battery_flags;
  int        battery_percentage;
  int        battery_time;
  int        using_minutes;
} ApmInfos;

typedef enum { LIGHTOFF, LIGHTON } Light;


Pixmap pixmap;
Pixmap backdrop_on;
Pixmap backdrop_off;
Pixmap parts;
Pixmap mask;
static unsigned switch_authorized = True;
static ApmInfos cur_apm_infos;

typedef struct Conf {
  char *display;
  Light backlight;		// back-light color
  char *light_color;
  int update_interval;		// sec
  int alarm_level;
  bool is_windowed;
  bool is_broken_wm;
  char *cmd_notify;
  char *cmd_suspend;
  char *cmd_hibernate;
} Conf;

Conf conf = {
  .display = "",
  .backlight = LIGHTOFF,
  .light_color = NULL,
  .update_interval = 5,
  .alarm_level = 20,
  .is_windowed = false,
  .is_broken_wm = true,
  .cmd_notify = NULL,
  .cmd_suspend = "echo systemctl suspend",
  .cmd_hibernate = "echo systemctl hibernate"
};

/* prototypes */
static void update();
static void switch_light();
static void draw_timedigit(ApmInfos infos);
static void draw_pcdigit(ApmInfos infos);
static void draw_statusdigit(ApmInfos infos);
static void draw_pcgraph(ApmInfos infos);
static void parse_arguments(int argc, char **argv);
static void print_help(char *prog);
static void apm_getinfos(ApmInfos *infos);
int apm_read(ApmInfos *i);


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

  /* Parse CommandLine */
  parse_arguments(argc, argv);

  /* FIXME: Check for ACPI support */

  /* Initialize Application */
  apm_getinfos(&cur_apm_infos);
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

  if (!dockapp_xpm2pixmap(backlight_on_xpm, &backdrop_on, &mask, colors, ncolor)) {
    fprintf(stderr, "Error initializing backlit background image.\n");
    exit(1);
  }
  if (!dockapp_xpm2pixmap(backlight_off_xpm, &backdrop_off, NULL, NULL, 0)) {
    fprintf(stderr, "Error initializing background image.\n");
    exit(1);
  }
  if (!dockapp_xpm2pixmap(parts_xpm, &parts, NULL, colors, ncolor)) {
    fprintf(stderr, "Error initializing parts image.\n");
    exit(1);
  }

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
	case 1: switch_light(); break;
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
      update();
    }
  }

  return 0;
}


/* called by timer */
static void update() {
  static Light pre_backlight;
  static Bool in_alarm_mode = False;

  /* get current cpu usage in percent */
  apm_getinfos(&cur_apm_infos);

  /* alarm mode */
  if (cur_apm_infos.battery_percentage < conf.alarm_level) {
    if (!in_alarm_mode) {
      in_alarm_mode = True;
      pre_backlight = conf.backlight;
      system(conf.cmd_notify);
    }
    if ( (switch_authorized) ||
	 ( (! switch_authorized) && (conf.backlight != pre_backlight) ) ) {
      switch_light();
      return;
    }
  } else {
    if (in_alarm_mode) {
      in_alarm_mode = False;
      if (conf.backlight != pre_backlight) {
	switch_light();
	return;
      }
    }
  }

  /* all clear */
  if (conf.backlight == LIGHTON)
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, 58, 58, 0, 0);
  else
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, 58, 58, 0, 0);

  /* draw digit */
  draw_timedigit(cur_apm_infos);
  draw_pcdigit(cur_apm_infos);
  draw_statusdigit(cur_apm_infos);
  draw_pcgraph(cur_apm_infos);

  /* show */
  dockapp_copy2window(pixmap);
}


/* called when mouse button pressed */
static void switch_light() {
  switch (conf.backlight) {
  case LIGHTOFF:
    conf.backlight = LIGHTON;
    dockapp_copyarea(backdrop_on, pixmap, 0, 0, 58, 58, 0, 0);
    break;
  case LIGHTON:
    conf.backlight = LIGHTOFF;
    dockapp_copyarea(backdrop_off, pixmap, 0, 0, 58, 58, 0, 0);
    break;
  }

  /* redraw digit */
  apm_getinfos(&cur_apm_infos);
  draw_timedigit(cur_apm_infos);
  draw_pcdigit(cur_apm_infos);
  draw_statusdigit(cur_apm_infos);
  draw_pcgraph(cur_apm_infos);

  /* show */
  dockapp_copy2window(pixmap);
}


static void draw_timedigit(ApmInfos infos) {
  int y = 0;
  int time_left, hour_left, min_left;

  if (conf.backlight == LIGHTON) y = 20;

  /*
    if (
    (infos.battery_time >= ((infos.using_minutes) ? 1440 : 86400))
    ) {
    copyXPMArea(83, 106, 41, 9, 15, 7);
    } else if (infos.battery_time >= 0) {
  */
  time_left = (infos.using_minutes) ? infos.battery_time : infos.battery_time / 60;
  hour_left = time_left / 60;
  min_left  = time_left % 60;
  dockapp_copyarea(parts, pixmap, (hour_left / 10) * 10, y, 10, 20,  5, 7);
  dockapp_copyarea(parts, pixmap, (hour_left % 10) * 10, y, 10, 20, 17, 7);
  dockapp_copyarea(parts, pixmap, (min_left / 10)  * 10, y, 10, 20, 32, 7);
  dockapp_copyarea(parts, pixmap, (min_left % 10)  * 10, y, 10, 20, 44, 7);
  /*
    }
  */
}


static void draw_pcdigit(ApmInfos infos) {
  int v100, v10, v1;
  int xd = 0;
  int num = infos.battery_percentage;

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


static void draw_statusdigit(ApmInfos infos) {
  int xd = 0;
  int y = 31;

  if (conf.backlight == LIGHTON) {
    y = 40;
    xd = 50;
  }

  /* draw digit */
  if (infos.battery_status == 3) /* charging */
    dockapp_copyarea(parts, pixmap, 100, y, 4, 9, 41, 45);

  if (infos.ac_line_status == 1)
    dockapp_copyarea(parts, pixmap, 0 + xd, 49, 5, 9, 34, 45);
  else
    dockapp_copyarea(parts, pixmap, 5 + xd, 49, 5, 9, 48, 45);
}


static void draw_pcgraph(ApmInfos infos) {
  int xd = 100;
  int nb;
  int num = infos.battery_percentage / 6.25 ;

  if (num < 0) num = 0;

  if (conf.backlight == LIGHTON) xd = 102;

  /* draw digit */
  for (nb = 0 ; nb < num ; nb++)
    dockapp_copyarea(parts, pixmap, xd, 0, 2, 9, 6 + nb * 3, 33);
}


static void parse_arguments(int argc, char **argv) {
  int i;
  int integer;
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      print_help(argv[0]), exit(0);
    } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
      printf("%s version %s\n", PACKAGE, VERSION), exit(0);
    } else if (!strcmp(argv[i], "--display") || !strcmp(argv[i], "-d")) {
      conf.display = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--backlight") || !strcmp(argv[i], "-bl")) {
      conf.backlight = LIGHTON;
    } else if (!strcmp(argv[i], "--light-color") || !strcmp(argv[i], "-lc")) {
      conf.light_color = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--interval") || !strcmp(argv[i], "-i")) {
      if (argc == i + 1)
	fprintf(stderr, "%s: error parsing argument for option %s\n",
		argv[0], argv[i]), exit(1);
      if (sscanf(argv[i + 1], "%i", &integer) != 1)
	fprintf(stderr, "%s: error parsing argument for option %s\n",
		argv[0], argv[i]), exit(1);
      if (integer < 1)
	fprintf(stderr, "%s: argument %s must be >=1\n",
		argv[0], argv[i]), exit(1);
      conf.update_interval = integer;
      i++;
    } else if (!strcmp(argv[i], "--alarm") || !strcmp(argv[i], "-a")) {
      if (argc == i + 1)
	fprintf(stderr, "%s: error parsing argument for option %s\n",
		argv[0], argv[i]), exit(1);
      if (sscanf(argv[i + 1], "%i", &integer) != 1)
	fprintf(stderr, "%s: error parsing argument for option %s\n",
		argv[0], argv[i]), exit(1);
      if ( (integer < 0) || (integer > 100) )
	fprintf(stderr, "%s: argument %s must be >=0 and <=100\n",
		argv[0], argv[i]), exit(1);
      conf.alarm_level = integer;
      i++;
    } else if (!strcmp(argv[i], "--windowed") || !strcmp(argv[i], "-w")) {
      dockapp_iswindowed = True;
    } else if (!strcmp(argv[i], "--broken-wm") || !strcmp(argv[i], "-bw")) {
      dockapp_isbrokenwm = True;
    } else if (!strcmp(argv[i], "--notify") || !strcmp(argv[i], "-n")) {
      conf.cmd_notify = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--suspend") || !strcmp(argv[i], "-s")) {
      conf.cmd_suspend = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "--hibernate") || !strcmp(argv[i], "-H")) {
      conf.cmd_hibernate = argv[i + 1];
      i++;
    } else {
      fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[i]);
      print_help(argv[0]), exit(1);
    }
  }
}


static void print_help(char *prog)
{
  printf("Usage : %s [OPTIONS]\n"
	 "%s - Window Maker mails monitor dockapp\n"
	 "  -d,  --display <string>        display to use\n"
	 "  -bl, --backlight               turn on back-light\n"
	 "  -lc, --light-color <string>    back-light color(rgb:6E/C6/3B is default)\n"
	 "  -i,  --interval <number>       number of secs between updates (1 is default)\n"
	 "  -a,  --alarm <number>          low battery level when to raise alarm (20 is default)\n"
	 "  -h,  --help                    show this help text and exit\n"
	 "  -v,  --version                 show program version and exit\n"
	 "  -w,  --windowed                run the application in windowed mode\n"
	 "  -bw, --broken-wm               activate broken window manager fix\n"
	 "  -n,  --notify <string>         command to launch when alarm is on\n"
	 "  -s,  --suspend <string>        set command for suspend\n"
	 "  -H,  --hibernate <string>        set command for hibernation\n",
	 prog, prog);
  /* OPTIONS SUPP :
   *  ? -f, --file    : configuration file
   */
}


static void apm_getinfos(ApmInfos *infos) {
  if (!apm_read(infos)) {
    fprintf(stderr, "Cannot read APM information\n");
    exit(1);
  }
}


int apm_read(ApmInfos *i) {
  fprintf(stderr, "apm_read()\n");

  int *bt_list = battery_list();
  if (!bt_list) return false;

  Battery bt;
  battery_get(1, &bt);

  i->ac_line_status = bt.is_ac_power;
  i->battery_status = bt.is_charging;
  i->battery_percentage = bt.capacity;
  i->battery_time = bt.seconds_remaining;

  i->using_minutes = 0;
  return true;
}
