<!-- SPDX-FileCopyrightText: Copyright (c) 2026 misc-de -->
<!-- SPDX-License-Identifier: MIT -->
# Lockout indicator

A lock and the time left in phosh's top bar while the lock screen refuses PINs
after failed attempts - the lockout of
[furios_security](https://github.com/misc-de/furios_security)
(`secctl apply lockout`).

phosh drops every PAM message, so during a lock the right PIN is refused
without a word. The top bar is visible on the PIN page too, and says why.

It reads `~/.local/state/furios-lockout/state` (written by the PAM module),
learns of changes through a file monitor, and ticks only while a lock is on.
Anything it cannot read shows nothing.

    ./install.sh      # no sudo; asks for it where it writes to /usr/lib
    ./uninstall.sh

phosh loads plugins at start, so it appears after the next reboot.

The installer adds `furios-lockout` to phosh's plugin list itself; before it
does, the list as it was is recorded, and `uninstall.sh` puts that back -
see "What was there before" in the [top README](../README.md).

Tests: `tests/run-tests.sh` loads the plugin the way phosh does and moves a
state file of its own under it.
