#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Builds the lockout indicator (a lock and the time left in the top bar) and puts it where phosh looks for plugins. Run it
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
python3 -c 'import gi' 2>/dev/null \
    || missing+=("python3-gi (it keeps the record of the original state)")
if [ ${#missing[@]} -gt 0 ]; then
    printf 'Missing: %s\n' "${missing[@]}" >&2
    echo "Nothing was built." >&2
    exit 1
fi

echo "1) building"
make -C "$SRC" all

echo "2) installing"
# Before the first write, what the phone had: phosh's plugin list (and
# whether it had a value at all), every path this repository writes and
# which directories above them were missing. Taken once - a reinstall finds
# the record and keeps it - and read back by uninstall.sh. See
# lib/furios-phosh-original.
DESTDIR="${DESTDIR:-}" python3 "$SRC/../lib/furios-phosh-original" record
# DESTDIR, as in make: a staged root instead of /, for the tests.
sudo make -C "$SRC" install DESTDIR="${DESTDIR:-}"

# The guard that takes our plugins out again if a phosh update makes the
# shell crash on them - installed with every plugin of this repository.
"$SRC/../guard/install.sh"

DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins)
echo
echo "Installed in $DIR."
# Into the list at once, unlike the folder dock: this one shows nothing at
# all unless the lock screen is locked, and then it is the only place that
# says so. Only our own entry is added; the rest of the list stays as it is.
python3 - <<'PY'
import ast, subprocess
key = ["mobi.phosh.shell.plugins", "status-icons"]
text = subprocess.run(["gsettings", "get"] + key, capture_output=True,
                      text=True).stdout.strip()
if text.startswith("@as "):
    text = text[4:]
try:
    names = list(ast.literal_eval(text or "[]"))
except (ValueError, SyntaxError):
    names = []
if "furios-lockout" not in names:
    names.insert(0, "furios-lockout")
    subprocess.run(["gsettings", "set"] + key + [str(names)], check=False)
PY
echo "In the top bar while the lock screen is locked (secctl apply lockout)."
echo
echo "phosh looks for plugins only when it starts, so it appears after the"
echo "next reboot."
