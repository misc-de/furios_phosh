#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Takes the status icon out of phosh's plugin directory, and out of the
# setting that lists it. The guard goes too once no plugin of this repository
# is left, so a later install.sh finds the phone as it shipped. Run WITHOUT
# sudo.
#
# The plugin itself writes nothing: ~/.local/state/furios-lockout is the PAM
# module's (furios_security), read here and removed by secctl's uninstaller.
#
# DESTDIR, as in make: a staged root instead of /, for the tests.
set -uo pipefail

if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo." >&2
    exit 1
fi

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN="furios-lockout"
DESTDIR=${DESTDIR:-}

# Where make put it. pkg-config only knows while phosh-dev is installed, and
# that may be gone by now - with an empty PLUGIN_DIR the uninstall rule would
# delete nothing at all, and say nothing either.
PLUGIN_DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins 2>/dev/null) \
    || PLUGIN_DIR=
if [ -z "$PLUGIN_DIR" ]; then
    for d in "$DESTDIR"/usr/lib/*/phosh/plugins; do
        [ -e "$d/$PLUGIN.plugin" ] && PLUGIN_DIR=${d#"$DESTDIR"}
    done
fi

# The setting first, and only our own entry in it: a shell that goes on being
# told to load a plugin which is no longer there logs a warning on every
# start, and somebody else's plugin in the same list is none of our business.
# A list that ends up as it shipped is reset rather than written: a value in
# the user's dconf, even the default one, pins the key against whatever a
# later phosh ships, and a new phone has no value there at all.
if command -v gsettings >/dev/null; then
    python3 - "$PLUGIN" <<'PY' || true
import ast, os, subprocess, sys
key = ["mobi.phosh.shell.plugins", "status-icons"]


def read(env=None):
    out = subprocess.run(["gsettings", "get"] + key, capture_output=True,
                         text=True, env=env)
    text = out.stdout.strip()
    if text.startswith("@as "):
        text = text[4:]
    return list(ast.literal_eval(text))


try:
    names = [n for n in read() if n != sys.argv[1]]
except (ValueError, SyntaxError):
    sys.exit(0)
# The memory backend holds no values, so what it answers is the default.
try:
    default = read(dict(os.environ, GSETTINGS_BACKEND="memory"))
except (ValueError, SyntaxError):
    default = None
if names == default:
    subprocess.run(["gsettings", "reset"] + key, check=False)
else:
    subprocess.run(["gsettings", "set"] + key + [str(names)], check=False)
PY
fi

if [ -n "$PLUGIN_DIR" ]; then
    sudo make -C "$SRC" uninstall PLUGIN_DIR="$PLUGIN_DIR" DESTDIR="$DESTDIR"
else
    echo "phosh's plugin directory not found - nothing to take out of it."
fi

# The guard came with the first plugin of this repository, so it goes with
# the last one. Somebody else's furios-* plugin does not keep it: it was
# never installed for those.
left=0
for other in furios-folder-dock furios-lockout; do
    [ -n "$PLUGIN_DIR" ] && [ -e "$DESTDIR$PLUGIN_DIR/$other.plugin" ] && left=1
done
[ "$left" = 1 ] || DESTDIR="$DESTDIR" "$SRC/../guard/uninstall.sh"

echo "Removed. The shell drops it at its next start."
