/*
 *    WMApmLoad - A dockapp to monitor APM status
 *    Copyright (C) 2002  Thomas Nemeth <tnemeth@free.fr>
 *
 *    Based on work by Seiichi SATO <ssato@sh.rim.or.jp>
 *    Copyright (C) 2001,2002  Seiichi SATO <ssato@sh.rim.or.jp>
 *    and on work by Mark Staggs <me@markstaggs.net>
 *    Copyright (C) 2002  Mark Staggs <me@markstaggs.net>

 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.

 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.

 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define PACKAGE "wmapmload"
#define VERSION "0.3.4"

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

#define FREE(data) {if (data) free (data); data = NULL;}

#define SIZE	    58
#define MAXSTRLEN   512
#define WINDOWED_BG ". c #AEAAAE"
#define MAX_HISTORY 16
#define CPUNUM_NONE -1

#define APMDEV "/proc/apm"

#define SUSPEND_CMD "apm -s"
#define STANDBY_CMD "apm -S"

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

typedef enum { LIGHTOFF, LIGHTON } light;


Pixmap pixmap;
Pixmap backdrop_on;
Pixmap backdrop_off;
Pixmap parts;
Pixmap mask;
static char	*display_name     = "";
static char	*light_color      = NULL;	/* back-light color */
static unsigned update_interval   = 1;
static light    backlight         = LIGHTOFF;
static unsigned switch_authorized = True;
static unsigned alarm_level       = 20;
static char     *notif_cmd        = NULL;
static char     *suspend_cmd      = NULL;
static char     *standby_cmd      = NULL;

static ApmInfos cur_apm_infos;

#ifndef APM_32_BIT_SUPPORT
# define APM_32_BIT_SUPPORT      0x0002
#endif


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
static int  my_system (char *cmd);
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
    dockapp_open_window(display_name, PACKAGE, SIZE, SIZE, argc, argv);
    dockapp_set_eventmask(ButtonPressMask);

    if (light_color) {
        colors[0].pixel = dockapp_getcolor(light_color);
        colors[1].pixel = dockapp_blendedcolor(light_color, -24, -24, -24, 1.0);
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
    if (backlight == LIGHTON)
        dockapp_copyarea(backdrop_on, pixmap, 0, 0, SIZE, SIZE, 0, 0);
    else
        dockapp_copyarea(backdrop_off, pixmap, 0, 0, SIZE, SIZE, 0, 0);

    dockapp_set_background(pixmap);
    dockapp_show();

    /* Main loop */
    while (1) {
        if (dockapp_nextevent_or_timeout(&event, update_interval * 1000)) {
            /* Next Event */
            switch (event.type) {
                case ButtonPress:
                    switch (event.xbutton.button) {
                        case 1: switch_light(); break;
                        case 2:
#ifndef SunOS
                            if (event.xbutton.state == ControlMask)
                                my_system(suspend_cmd ? suspend_cmd : SUSPEND_CMD); /* Suspend */
                            else
                                my_system(standby_cmd ? standby_cmd : STANDBY_CMD); /* Standby */
#endif
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
    static light pre_backlight;
    static Bool in_alarm_mode = False;

    /* get current cpu usage in percent */
    apm_getinfos(&cur_apm_infos);

    /* alarm mode */
    if (cur_apm_infos.battery_percentage < alarm_level) {
        if (!in_alarm_mode) {
            in_alarm_mode = True;
            pre_backlight = backlight;
            my_system(notif_cmd);
        }
        if ( (switch_authorized) ||
             ( (! switch_authorized) && (backlight != pre_backlight) ) ) {
            switch_light();
            return;
        }
    } else {
        if (in_alarm_mode) {
            in_alarm_mode = False;
            if (backlight != pre_backlight) {
                switch_light();
                return;
            }
        }
    }

    /* all clear */
    if (backlight == LIGHTON)
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
    switch (backlight) {
        case LIGHTOFF:
            backlight = LIGHTON;
            dockapp_copyarea(backdrop_on, pixmap, 0, 0, 58, 58, 0, 0);
            break;
        case LIGHTON:
            backlight = LIGHTOFF;
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

    if (backlight == LIGHTON) y = 20;

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

    if (backlight == LIGHTON) xd = 50;

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

    if (backlight == LIGHTON) {
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

    if (backlight == LIGHTON) xd = 102;

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
            display_name = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "--backlight") || !strcmp(argv[i], "-bl")) {
            backlight = LIGHTON;
        } else if (!strcmp(argv[i], "--light-color") || !strcmp(argv[i], "-lc")) {
            light_color = argv[i + 1];
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
            update_interval = integer;
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
            alarm_level = integer;
            i++;
        } else if (!strcmp(argv[i], "--windowed") || !strcmp(argv[i], "-w")) {
            dockapp_iswindowed = True;
        } else if (!strcmp(argv[i], "--broken-wm") || !strcmp(argv[i], "-bw")) {
            dockapp_isbrokenwm = True;
        } else if (!strcmp(argv[i], "--notify") || !strcmp(argv[i], "-n")) {
            notif_cmd = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "--suspend") || !strcmp(argv[i], "-s")) {
            suspend_cmd = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "--standby") || !strcmp(argv[i], "-S")) {
            standby_cmd = argv[i + 1];
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
           "  -s,  --suspend <string>        set command for apm suspend\n"
           "  -S,  --standby <string>        set command for apm standby\n",
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

static int my_system (char *cmd) {
    int           pid;
    extern char **environ;

    if (cmd == 0) return 1;
    pid = fork ();
    if (pid == -1) return -1;
    if (pid == 0) {
        pid = fork ();
        if (pid == 0) {
            char *argv[4];
            argv[0] = "sh";
            argv[1] = "-c";
            argv[2] = cmd;
            argv[3] = 0;
            execve ("/bin/sh", argv, environ);
            exit (0);
        }
        exit (0);
    }
    return 0;
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
