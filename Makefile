CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LDLIBS   = -lwayland-client -lm -lpthread
SCANNER  = wayland-scanner

# make BITMAP=1 builds with the embedded 8x8 pixel font instead of ttf
ifdef BITMAP
CFLAGS += -DBITMAP_FONT
endif

PROTOS = ext-workspace-v1 wlr-layer-shell-unstable-v1 xdg-shell
HDRS   = $(PROTOS:%=%-client-protocol.h)
PSRCS  = $(PROTOS:%=%-protocol.c)

mbar: bar.c config.h modules.h $(HDRS) $(PSRCS)
	$(CC) $(CFLAGS) -o $@ bar.c $(PSRCS) $(LDLIBS)

config.h:
	cp config.def.h config.h

modules.h: gen-modules.sh $(wildcard modules/*)
	sh gen-modules.sh > modules.h

%-client-protocol.h: protocols/%.xml
	$(SCANNER) client-header $< $@

%-protocol.c: protocols/%.xml
	$(SCANNER) private-code $< $@

install: mbar
	install -Dm755 mbar /usr/local/bin/mbar

clean:
	rm -f mbar modules.h $(HDRS) $(PSRCS)

.PHONY: install clean
