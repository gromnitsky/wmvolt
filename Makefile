ifdef DEBUG
override DEBUG := -debug
override CFLAGS += -g
endif

CFLAGS += -std=c11 -Wall
override LDFLAGS += `pkg-config --libs xpm xext`

ifndef build.target
build.target := $(shell uname -m)
endif

compile:

out := _build.$(build.target)$(DEBUG)

obj := $(patsubst %.c, $(out)/%.o, $(wildcard *.c))
$(out)/main.o: $(wildcard *.xpm) $(wildcard *.h)
$(out)/battery.o: battery.h
$(out)/dockapp.o: dockapp.h

$(out)/%.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) $< -o $@

$(out)/wmvolt: $(obj)
	$(LINK.c) $^ -o $@

compile: $(out)/wmvolt
