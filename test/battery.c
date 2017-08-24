#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include "../battery.h"

int main(int argc, char *argv[])
{
  Battery bt;
  bool r;
  if (argc > 1) {
    r = battery_get_from_file(argv[1], &bt);
  } else {
    errx(1, "Usage: %s file.txt", argv[0]);
  }
  if (!r) err(1, "epic fail");

  printf("%d %d %d %d:%d\n",
	 bt.is_charging,
	 bt.capacity,
	 bt.seconds_remaining,
	 bt.seconds_remaining / 3600, bt.seconds_remaining / 60 % 60);
  return 0;
}
