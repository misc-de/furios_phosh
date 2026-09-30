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
| [guard](guard/) | a shell guard | If phosh restarts within a minute of its last start (a plugin no longer fits after a phosh update), every `furios-*` plugin comes out of the plugin lists and the shell runs as shipped; `furios-phosh-guard restore` puts them back. Installed with every plugin here |

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
- **A way back.** `uninstall.sh` in every directory - and back to what the
  phone had, not to what a new phone probably has: see below.
- **Tests run on the phone**, without root. The plugins are GTK widgets and
  need a display to be built on; without one the suite says so and skips
  rather than fails.

## What was there before

Before the first installer of this repository writes anything, it writes
down what the phone had, in

    ~/.local/state/furios-phosh/original.json     (${XDG_STATE_HOME}/furios-phosh)

- phosh's plugin list `mobi.phosh.shell.plugins status-icons`: whether it had
  a value in dconf at all, and which. "No value" and "the default written as
  a value" look the same to `gsettings get`, but only the first follows a
  later phosh's default - so unset goes back to unset (`gsettings reset`),
  and a value goes back to exactly that value, even when it equals the
  default;
- every path this repository writes (the two plugins in phosh's plugin
  directory, `/usr/local/libexec/furios-phosh-guard`, the shell unit's
  drop-in, the dock's crash mark and settings file, the guard's state) and
  whether it was there;
- the directories above them that were missing - only those are taken out
  again, and only while empty. A directory that was there stays, empty or
  not.

The record is taken once: a reinstall, or the other plugin's installer,
finds it and keeps it. The uninstaller of the last plugin puts the list back
from it and removes it. If the list was changed since the install (somebody
added a plugin), only our names come out, and the uninstaller says so. If
there is no record - installed before records were kept - the uninstallers
do what they did before (compare with phosh's default) and say that this is
a guess. `lib/furios-phosh-original status` shows the record.

The folder dock's entry is switched by [furios_app][p], which also writes
`~/.config/furios-folder-dock.conf`. The app offers that switch only once
the plugin is installed, so the installer's record is always older than the
app's first change, and the uninstall covers the app's changes as well.

The guard keeps a record of its own: the list it took our names out of,
and the list it wrote instead. `furios-phosh-guard restore` puts the first
back only while the second is still what is set - a list somebody changed
after the guard stepped in is left alone and both are printed.

## Tests

```
./run-tests.sh          # every subproject - NEVER with sudo
```
