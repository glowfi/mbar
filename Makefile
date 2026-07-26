CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LDLIBS   = -lwayland-client -lm
SCANNER  = wayland-scanner

PROTOS = ext-workspace-v1 wlr-layer-shell-unstable-v1 xdg-shell
HDRS   = $(PROTOS:%=%-client-protocol.h)
PSRCS  = $(PROTOS:%=%-protocol.c)

mbar: bar.c $(HDRS) $(PSRCS)
	$(CC) $(CFLAGS) -o $@ bar.c $(PSRCS) $(LDLIBS)

%-client-protocol.h: protocols/%.xml
	$(SCANNER) client-header $< $@

%-protocol.c: protocols/%.xml
	$(SCANNER) private-code $< $@

install: mbar
	install -Dm755 mbar /usr/local/bin/mbar

clean:
	rm -f mbar $(HDRS) $(PSRCS)

.PHONY: install clean
