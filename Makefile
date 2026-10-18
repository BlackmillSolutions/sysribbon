# SPDX-License-Identifier: MIT
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
DESTDIR ?=
CFLAGS ?= -O2
CPPFLAGS ?=
LDFLAGS ?=
PKGS = gtk+-3.0 libxfce4panel-2.0 libxfce4util-1.0
LIBDIR ?= $(shell $(PKG_CONFIG) --variable=libdir libxfce4panel-2.0)
PLUGIN_DIR ?= $(LIBDIR)/xfce4/panel/plugins
PANEL_META_DIR ?= $(PREFIX)/share/xfce4/panel/plugins
ICON_DIR ?= $(PREFIX)/share/icons/hicolor/128x128/apps
BINARY = libsysribbon.so

.PHONY: all check-deps install uninstall clean test
all: check-deps $(BINARY)

check-deps:
	@$(PKG_CONFIG) --exists $(PKGS) || { \
	  echo 'Missing GTK3/Xfce development libraries.' >&2; \
	  echo 'Debian/Ubuntu: sudo apt install build-essential pkg-config libgtk-3-dev libxfce4ui-2-dev libxfce4panel-2.0-dev libxfce4util-dev' >&2; \
	  exit 1; }

$(BINARY): src/plugin.c src/sampler.c src/sampler.h src/presentation.c src/presentation.h | check-deps
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -fPIC -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
	  $(shell $(PKG_CONFIG) --cflags $(PKGS)) -shared -o $@ \
	  src/plugin.c src/sampler.c src/presentation.c \
	  $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(PKGS)) -lm

install: check-deps $(BINARY)
	install -d $(DESTDIR)$(PLUGIN_DIR) $(DESTDIR)$(PANEL_META_DIR) $(DESTDIR)$(ICON_DIR)
	install -m 755 $(BINARY) $(DESTDIR)$(PLUGIN_DIR)/$(BINARY)
	install -m 644 sysribbon.desktop $(DESTDIR)$(PANEL_META_DIR)/sysribbon.desktop
	install -m 644 assets/sysribbon-icon.png $(DESTDIR)$(ICON_DIR)/sysribbon.png

uninstall:
	rm -f $(DESTDIR)$(PLUGIN_DIR)/$(BINARY) \
	      $(DESTDIR)$(PANEL_META_DIR)/sysribbon.desktop \
	      $(DESTDIR)$(ICON_DIR)/sysribbon.png

test:
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	  -o test-sampler tests/test_sampler.c src/sampler.c -lm
	./test-sampler
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	  -o test-fixture tests/test_fixture.c src/sampler.c -lm
	./test-fixture
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	  -o test-presentation tests/test_presentation.c src/presentation.c -lm
	./test-presentation
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	  -o test-scales tests/test_scales.c src/presentation.c -lm
	./test-scales

clean:
	rm -f $(BINARY) test-sampler test-fixture test-presentation test-scales
