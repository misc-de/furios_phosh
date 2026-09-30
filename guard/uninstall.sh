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
DESTDIR=${DESTDIR:-}
DROPIN_DIR="$DESTDIR/etc/systemd/user/mobi.phosh.Shell.service.d"
sudo rm -f "$DESTDIR/usr/local/libexec/furios-phosh-guard" \
    "$DROPIN_DIR/50-furios-phosh-guard.conf"
sudo rmdir "$DROPIN_DIR" 2>/dev/null || true
# install -D made /usr/local/libexec for the guard: Debian's base-files does
# not create it, and nothing else of ours puts anything there. Only while
# it is empty, so whatever somebody else keeps there holds it.
sudo rmdir "$DESTDIR/usr/local/libexec" 2>/dev/null || true
systemctl --user daemon-reload 2>/dev/null || true
# The time of the last shell start, and the plugin lists it took our names
# out of. Those lists are not put back: they name plugins that are going or
# gone, and "restore" would hand the shell names it can no longer load.
rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/furios-phosh-guard"
echo "phosh guard removed."
