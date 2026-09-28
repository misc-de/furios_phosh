# furios_phosh — phosh's app overview, rearranged, for the FuriPhone FLX1

Changes to how phosh's app overview looks, as phosh plugins that change
nothing on disk that belongs to FuriOS and put everything back when they are
switched off. All of it is operated from [furios_app][p], tab "Phosh".

Small things with no plugin behind them live in [furios_misc][m].

[p]: https://github.com/misc-de/furios_app
[m]: https://github.com/misc-de/furios_misc

## What is in here

| | | |
|---|---|---|
| [folder-dock](folder-dock/) | a phosh plugin | The overview's folders in a bar at the bottom edge, the apps blurred underneath; optionally one row, and apps without their names |
| [lockout-status](lockout-status/) | a phosh plugin | A lock and the time left in the top bar while the lock screen refuses PINs after failed attempts (furios_security's lockout); its own widget only, so no crash guard needed |

Each directory stands on its own: its own README, its own `install.sh`, its
own tests.

```
git clone https://github.com/misc-de/furios_phosh
cd furios_phosh/folder-dock && ./install.sh      # WITHOUT sudo
```

phosh looks for plugins only when it starts, so a new plugin is picked up at
the next reboot.

## House rules

- **Nothing is patched.** A plugin works from inside phosh's process on
  phosh's own widgets and leaves them as it found them when it goes.
- **A crash guard.** A crash of phosh ends the session; every plugin that
  touches phosh's widgets marks its attempt and stands down after a crash.
- **An SPDX header in every file**, MIT (see LICENSE).
- **A way back.** `uninstall.sh` in every directory.
- **Tests run on the phone**, without root. The plugins are GTK widgets and
  need a display to be built on; without one the suite says so and skips
  rather than fails.

## Tests

```
./run-tests.sh          # every subproject - NEVER with sudo
```
