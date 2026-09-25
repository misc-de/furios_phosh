# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# One C file, one shared object, and the two paths phosh looks in - which
# come from phosh's own pkg-config file rather than from a guess about the
# architecture: on this phone the directory is
# /usr/lib/aarch64-linux-gnu/phosh/plugins, and nothing here has to know that.

PLUGIN      := furios-folder-dock
SO          := libphosh-plugin-$(PLUGIN).so
PKGS        := gtk+-3.0 gio-2.0 phosh-plugins
PLUGIN_DIR  := $(shell pkg-config --variable=status_icons_plugins_dir phosh-plugins)

CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -Wno-unused-parameter -fPIC $(shell pkg-config --cflags $(PKGS))
LDFLAGS += -shared
LDLIBS  += $(shell pkg-config --libs $(PKGS))

all: $(SO) $(PLUGIN).plugin

$(SO): folder-dock.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

# The .plugin file names the .so by its full path, and that path is the one
# pkg-config gives - so it is written here rather than kept as a file with a
# directory baked into it.
$(PLUGIN).plugin: plugin.in Makefile
	sed 's|@PLUGIN_DIR@|$(PLUGIN_DIR)|' $< > $@

install: all
	install -Dm644 $(SO) $(DESTDIR)$(PLUGIN_DIR)/$(SO)
	install -Dm644 $(PLUGIN).plugin $(DESTDIR)$(PLUGIN_DIR)/$(PLUGIN).plugin

uninstall:
	rm -f $(DESTDIR)$(PLUGIN_DIR)/$(SO) $(DESTDIR)$(PLUGIN_DIR)/$(PLUGIN).plugin

# The test builds against the same headers and loads the built .so the way
# the shell does. It needs a display, and says so rather than failing when
# there is none - see tests/run-tests.sh.
# -ldl: the test opens phosh's library itself, the way the shell's process
# has it open already - without it there is no status icon to derive from.
tests/dock-works: tests/dock-works.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS) -ldl

check: $(SO) tests/dock-works
	./tests/dock-works .

clean:
	rm -f $(SO) $(PLUGIN).plugin tests/dock-works

.PHONY: all install uninstall check clean
