#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Builds the folder dock (a status icon that shows nothing) and puts it where phosh looks for plugins. Run it
# WITHOUT sudo - the two lines that write to /usr/lib ask for themselves.
#
# Why this one needs root when the rest of this project does not: phosh takes
# its plugin directory from a compile-time constant (PHOSH_PLUGINS_DIR), so
# there is no directory in the home the shell would look in. Checked against
# phosh 0.55.
set -euo pipefail

if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo - only the install step needs root." >&2
    exit 1
fi

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

missing=()
command -v cc >/dev/null || missing+=("a C compiler (apt install build-essential)")
command -v make >/dev/null || missing+=("make (apt install build-essential)")
pkg-config --exists phosh-plugins 2>/dev/null \
    || missing+=("phosh's plugin headers (apt install phosh-dev)")
pkg-config --exists gtk+-3.0 2>/dev/null \
    || missing+=("GTK 3 headers (apt install libgtk-3-dev)")
if [ ${#missing[@]} -gt 0 ]; then
    printf 'Missing: %s\n' "${missing[@]}" >&2
    echo "Nothing was built." >&2
    exit 1
fi

echo "1) building"
make -C "$SRC" all

echo "2) installing"
# DESTDIR, as in make: a staged root instead of /, for the tests.
sudo make -C "$SRC" install DESTDIR="${DESTDIR:-}"

# The guard that takes our plugins out again if a phosh update makes the
# shell crash on them - installed with every plugin of this repository.
"$SRC/../guard/install.sh"

DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins)
echo
echo "Installed in $DIR."
# Installed, not switched on: the switch is in the
# misc-de app under "Phosh", or:
#   gsettings set mobi.phosh.shell.plugins status-icons "[..., 'furios-folder-dock']"
echo "Switch it on in the misc-de app under \"Phosh\" -> \"Folders at the bottom\"."
echo
echo "phosh looks for plugins only when it starts, so this one is picked up"
echo "at the next reboot. After that the switch takes effect at once."
