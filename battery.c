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

static
long mAh_to_mWh(long voltage, long val) {
  return (voltage/1000.0) * (val/1000.0);
}

typedef struct Uevent {
  bool is_charging;
  long power;
  int capacity;
  long energy_now;
  long energy_full;
  long energy_full_design;
  long voltage;
  bool is_mWh;
} Uevent;

void uevent_init(Uevent *ue) {
  ue->is_charging = false;
  ue->power = -1;
  ue->capacity = -1;
  ue->energy_now = -1;
  ue->energy_full = -1;
  ue->energy_full_design = -1;
  ue->voltage = -1;
  ue->is_mWh = true;
}

static
bool parse_entry(char *file, Uevent *uevent) {
  FILE *fp = fopen(file, "r");
  if (!fp) return false;

  char *line = NULL;
  size_t line_size = 0;
  while (getline(&line, &line_size, fp) != -1) {
    char key[BUFSIZ], val[BUFSIZ];
    sscanf(line, "%[^=]=%s", key, val);
    //printf("'%s' = '%s'\n", key, val);

    if (strcmp("POWER_SUPPLY_STATUS", key) == 0
	&& strcasecmp("charging", val) == 0) uevent->is_charging = true;
    if (strcmp("POWER_SUPPLY_CAPACITY", key) == 0) uevent->capacity = atoi(val);
    if (strcmp("POWER_SUPPLY_VOLTAGE_NOW", key) == 0)
      uevent->voltage = atoi(val);

    if (strcmp("POWER_SUPPLY_POWER_NOW", key) == 0
	|| strcmp("POWER_SUPPLY_CURRENT_NOW", key) == 0)
      uevent->power = abs(atoi(val));
    if (strcmp("POWER_SUPPLY_ENERGY_FULL", key) == 0
	|| strcmp("POWER_SUPPLY_CHARGE_FULL", key) == 0)
      uevent->energy_full = atoi(val);
    if (strcmp("POWER_SUPPLY_ENERGY_FULL_DESIGN", key) == 0
	|| strcmp("POWER_SUPPLY_CHARGE_FULL_DESIGN", key) == 0)
      uevent->energy_full = atoi(val);

    if (strcmp("POWER_SUPPLY_ENERGY_NOW", key) == 0) {
      uevent->is_mWh = true;
      uevent->energy_now = atoi(val);
    }
    if (strcmp("POWER_SUPPLY_CHARGE_NOW", key) == 0) {
      uevent->is_mWh = false;
      uevent->energy_now = atoi(val);
    }

    key[0] = '\0';
    val[0] = '\0';
  }
  free(line);
  fclose(fp);
  return true;
}

bool battery_get_from_file(char *file, Battery *bt) {
  Uevent uevent;
  uevent_init(&uevent);
  if (!parse_entry(file, &uevent)) return false;

  battery_init(bt);
  bt->is_ac_power = ac_power() == 1;
  bt->is_charging = uevent.is_charging;
  bt->capacity = uevent.capacity;

  if (uevent.energy_now > uevent.energy_full)
    uevent.energy_now = uevent.energy_full;
  if (uevent.energy_full == -1) uevent.energy_full = uevent.energy_full_design;

  // ENERGY_* attrs represents capacity in mWh;
  // CHARGE_* attrs represents capacity in mAh;
  if (!uevent.is_mWh && uevent.voltage != -1) {
    // actually this is not necessary
    uevent.power = mAh_to_mWh(uevent.voltage, uevent.power);
    uevent.energy_now = mAh_to_mWh(uevent.voltage, uevent.energy_now);
    uevent.energy_full = mAh_to_mWh(uevent.voltage, uevent.energy_full);
  }

  if (uevent.power > 0 && uevent.energy_now > 0 && uevent.energy_full > 0
      && bt->capacity <= 100) {
    double hours;
    if (bt->is_charging) {
      hours = (uevent.energy_full - uevent.energy_now) / (double)uevent.power;
    } else {
      hours = (double)uevent.energy_now / uevent.power;
    }
    bt->seconds_remaining = (int)(hours * 60*60);
  } else
    bt->seconds_remaining = 0;

  if (uevent.energy_now > 0 && uevent.energy_full > 0) {
    int percent = ((double)uevent.energy_now/uevent.energy_full)*100;
    if (bt->capacity < 0 || bt->capacity >= 100) bt->capacity = percent;
  }
  if (bt->capacity > 100) bt->capacity = 100;
  return true;
}

int *battery_list() {
  int *list = NULL;
  glob_t gbuf;

  if (glob("/sys/class/power_supply/BAT*", 0, NULL, &gbuf) != 0)
    return NULL;

  size_t size = gbuf.gl_pathc;
  list = malloc((size + 1) * sizeof(int));
  for (size_t i = 0; i < size; ++i)
    list[i] = atoi(basename(gbuf.gl_pathv[i])+3);
  list[size] = -1;

  globfree(&gbuf);
  return list;
}
