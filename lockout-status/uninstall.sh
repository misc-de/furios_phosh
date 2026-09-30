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

# Is this the last plugin of this repository? Then the list goes back to
# what the record says the phone had before the first install, and the guard
# and the record go too. Otherwise only our own name comes out.
ORIG="$SRC/../lib/furios-phosh-original"
final=1
for other in furios-folder-dock furios-lockout; do
    [ "$other" = "$PLUGIN" ] && continue
    [ -n "$PLUGIN_DIR" ] && [ -e "$DESTDIR$PLUGIN_DIR/$other.plugin" ] && final=0
done

# The setting first, and only our own entry in it: a shell that goes on being
# told to load a plugin which is no longer there logs a warning on every
# start, and somebody else's plugin in the same list is none of our business.
#
# What the list goes back to is not guessed: lib/furios-phosh-original
# recorded it before the first install - whether the key had a value in
# dconf at all, and which. Unset goes back to unset (`gsettings reset`), a
# value goes back to that value, even when it equals the default. A list
# somebody changed since is left as it is, with only our names taken out,
# and that is said. Without a record (installed before records were kept)
# it compares with the schema default as it used to, and says that too.
if [ "$final" = 1 ]; then
    DESTDIR="$DESTDIR" python3 "$ORIG" key-remove "$PLUGIN" --final || true
else
    DESTDIR="$DESTDIR" python3 "$ORIG" key-remove "$PLUGIN" || true
fi

if [ -n "$PLUGIN_DIR" ]; then
    sudo make -C "$SRC" uninstall PLUGIN_DIR="$PLUGIN_DIR" DESTDIR="$DESTDIR"
else
    echo "phosh's plugin directory not found - nothing to take out of it."
fi

# The guard came with the first plugin of this repository, so it goes with
# the last one. Somebody else's furios-* plugin does not keep it: it was
# never installed for those. The guard's uninstaller also takes out the
# directories the record says were missing, and then the record itself.
[ "$final" = 1 ] && DESTDIR="$DESTDIR" "$SRC/../guard/uninstall.sh"

echo "Removed. The shell drops it at its next start."
