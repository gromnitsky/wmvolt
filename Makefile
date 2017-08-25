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

mkdir = @mkdir -p $(dir $@)


obj := $(patsubst %.c, $(out)/%.o, $(wildcard *.c))
$(out)/main.o: $(wildcard *.xpm) $(wildcard *.h)
$(out)/battery.o: battery.h
$(out)/dockapp.o: dockapp.h

$(out)/%.o: %.c
	$(mkdir)
	$(COMPILE.c) $< -o $@

$(out)/wmvolt: $(obj)
	$(LINK.c) $^ -o $@

compile: $(out)/wmvolt



$(out)/test/battery: test/battery.c $(out)/battery.o
	$(mkdir)
	$(CC) $(CFLAGS) $(TARGET_ARCH) $^ -o $@

compile: $(out)/test/battery



doc.man := $(patsubst %.asciidoc, $(out)/%, $(wildcard *.asciidoc))
doc.html := $(patsubst %.asciidoc, $(out)/%.html, $(wildcard *.asciidoc))

$(out)/%.1.html $(out)/%.1: %.1.asciidoc
	$(mkdir)
	a2x --doctype manpage -f manpage $< -D $(dir $@)
	a2x --doctype manpage -f xhtml $< -D $(dir $@)

compile: $(doc.man) $(doc.html)
