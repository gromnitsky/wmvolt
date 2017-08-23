#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>

typedef struct Battery {
  int id;
  bool is_ac_power;
  bool is_charging;
  int capacity; // %
  int seconds_remaining;
} Battery;

void battery_init(Battery*);

// return false on error
bool battery_get(int, Battery*);
// return a -1-terminated array or NULL on error;
// the result should be free()'ed
int *battery_list();

#endif
