#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include "battery.h"

void battery_init(Battery *bt) {
  bt->id = -1;
  bt->is_ac_power = false;
  bt->is_charging = false;
  bt->capacity = -1;
  bt->seconds_remaining = -1;
}

static
int ac_power() {
  FILE *fp = fopen("/sys/class/power_supply/ACAD/online", "r");
  if (!fp) return -1;
  int ch = getc(fp);
  fclose(fp);

  if (ch == EOF) return -1;
  return ch == '1' ? 1 : 0;
}

bool battery_get(int id, Battery *bt) {
  char file[BUFSIZ];
  sprintf(file, "/sys/class/power_supply/BAT%d/uevent", id);
  bool r = battery_get_from_file(file, bt);
  bt->id = id;
  return r;
}

bool battery_get_from_file(char *file, Battery *bt) {
  FILE *fp = fopen(file, "r");
  if (!fp) return false;

  battery_init(bt);
  bt->is_ac_power = ac_power() == 1;

  char *line = NULL;
  size_t line_size = 0;
  int power = -1, energy = -1, energy_full = -1;
  while (getline(&line, &line_size, fp) != -1) {
    char key[BUFSIZ], val[BUFSIZ];
    sscanf(line, "%[^=]=%s", key, val);
    //printf("'%s' = '%s'\n", key, val);

    if (strcmp("POWER_SUPPLY_STATUS", key) == 0
	&& strcasecmp("charging", val) == 0) bt->is_charging = true;
    if (strcmp("POWER_SUPPLY_POWER_NOW", key) == 0) power = atoi(val);
    if (strcmp("POWER_SUPPLY_ENERGY_NOW", key) == 0) energy = atoi(val);
    if (strcmp("POWER_SUPPLY_ENERGY_FULL", key) == 0) energy_full = atoi(val);
    if (strcmp("POWER_SUPPLY_CAPACITY", key) == 0) bt->capacity = atoi(val);

    key[0] = '\0';
    val[0] = '\0';
  }
  free(line);

  if (power > 0 && energy > 0 && energy_full > 0) {
    double hours;
    if (bt->is_charging) {
      hours = (energy_full - energy) / (double)power;
    } else {
      hours = (double)energy / power;
    }
    bt->seconds_remaining = (int)(hours * 60*60);
  } else
    bt->seconds_remaining = 0;

  fclose(fp);
  return true;
}

int *battery_list() {
  int *list = NULL;
  glob_t gbuf;

  if (glob("/sys/class/power_supply/BAT*", GLOB_NOSORT, NULL, &gbuf) != 0)
    return NULL;

  size_t size = gbuf.gl_pathc;
  list = malloc((size + 1) * sizeof(int));
  for (size_t i = 0; i < size; ++i)
    list[i] = atoi(basename(gbuf.gl_pathv[i])+3);
  list[size] = -1;

  globfree(&gbuf);
  return list;
}
