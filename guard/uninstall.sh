#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Removes the guard: the script, the drop-in that runs it before the shell,
# and what it kept in ~/.local/state. Run WITHOUT sudo. The plugins'
# uninstallers call this once the last of them is gone.
#
# DESTDIR, as in make: a staged root instead of /, for the tests.
set -euo pipefail
if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo." >&2
    exit 1
fi
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIG="$SRC/../lib/furios-phosh-original"
DESTDIR=${DESTDIR:-}
DROPIN_DIR="$DESTDIR/etc/systemd/user/mobi.phosh.Shell.service.d"
sudo rm -f "$DESTDIR/usr/local/libexec/furios-phosh-guard" \
    "$DROPIN_DIR/50-furios-phosh-guard.conf"
systemctl --user daemon-reload 2>/dev/null || true

# Is a plugin of ours still installed? Then this was run by hand, and the
# record of the original state is still needed by that plugin's uninstaller:
# the directories and the record stay.
PLUGIN_DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins 2>/dev/null) \
    || PLUGIN_DIR=
left=0
for other in furios-folder-dock furios-lockout; do
    for d in ${PLUGIN_DIR:+"$DESTDIR$PLUGIN_DIR"} "$DESTDIR"/usr/lib/*/phosh/plugins; do
        [ -e "$d/$other.plugin" ] && left=1
    done
done

# The directories. With a record that can be trusted, exactly those that
# were missing before the first install - /usr/local/libexec, the drop-in
# directory, and phosh's plugin directory should an install have had to make
# it - and each only while empty, so whatever somebody put there since keeps
# it. A directory that was there before stays, empty or not: an empty
# directory is part of how the phone was too.
#
# Without one (an install from before records were kept) this guesses as it
# always did: the two directories the guard needs go while empty. Debian's
# base-files does not create /usr/local/libexec, and FuriOS ships no drop-in
# for the shell unit - both were true on this phone, and neither is known to
# be true on another.
if [ "$left" = 0 ]; then
    if DESTDIR="$DESTDIR" python3 "$ORIG" legacy 2>/dev/null; then
        echo "No full record of the original state (installed before records were"
        echo "  kept): removing the guard's directories while empty, as before - a guess."
        sudo rmdir "$DROPIN_DIR" 2>/dev/null || true
        sudo rmdir "$DESTDIR/usr/local/libexec" 2>/dev/null || true
    fi
    # What the record knows for certain - the directories that were missing
    # when it was taken - holds with or without an older install before it.
    while read -r d; do
        [ -n "$d" ] && { sudo rmdir "$DESTDIR$d" 2>/dev/null || true; }
    done < <(DESTDIR="$DESTDIR" python3 "$ORIG" dirs --system)
fi

# The time of the last shell start, and the plugin lists it took our names
# out of. Those lists are not put back from here: they name plugins that are
# going or gone. What the list goes back to is the record from before the
# first install, which the plugins' uninstallers have used by now.
rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/furios-phosh-guard"

# Last: the home's directories that were missing before the first install
# (while empty), and the record itself.
[ "$left" = 0 ] && DESTDIR="$DESTDIR" python3 "$ORIG" finish
echo "phosh guard removed."
