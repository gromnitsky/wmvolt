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

bool battery_get(int id, Battery *bt) {
  char db[BUFSIZ];
  sprintf(db, "/sys/class/power_supply/BAT%d/uevent", id);
  FILE *fp = fopen(db, "r");
  if (!fp) return false;

  battery_init(bt);

  char *line = NULL;
  size_t line_size = 0;
  int power = -1, energy = -1;
  while (getline(&line, &line_size, fp) != -1) {
    char key[BUFSIZ];
    int val = -1;
    sscanf(line, "%[^=]=%d", key, &val);

    if (strcmp("POWER_SUPPLY_POWER_NOW", key) == 0) {
      bt->is_ac_power = val == 0;
      bt->is_charging = !bt->is_ac_power; // FIXME
      power = val;
    }
    if (strcmp("POWER_SUPPLY_ENERGY_NOW", key) == 0) energy = val;
    if (strcmp("POWER_SUPPLY_CAPACITY", key) == 0) bt->capacity = val;

    key[0] = '\0';
    val = -1;
  }
  free(line);

  if (power > 0 && energy > 0) {
    double hours = (double)energy / power;
    bt->seconds_remaining = (int)(hours * 60*60);
  } else
    bt->seconds_remaining = 0;

  fclose(fp);
  bt->id = id;
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
