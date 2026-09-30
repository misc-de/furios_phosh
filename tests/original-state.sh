#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# The rule behind every uninstaller here, checked from the outside: before
# the first change the phone's state is written down, and the uninstall puts
# back exactly that - not what a new phone probably has. NEVER with sudo.
#
# Each case runs in a sandbox of its own and compares the sandbox with
# itself: snapshot, install, change things the way the app, the user and the
# guard do, uninstall, snapshot again. What the scripts do is not read here;
# what they leave is.
#
#   - a staged root (DESTDIR) and a home of their own; sudo runs only
#     install/rm/rmdir/mkdir and "make ... DESTDIR=<stage>", and refuses any
#     path outside the sandbox;
#   - gsettings is the real one, but on the keyfile backend and with phosh's
#     plugin schema compiled into the sandbox - so "unset" and "set to the
#     default" are the real distinction dconf makes, not a stand-in's idea
#     of it, and the phone's own dconf is never opened;
#   - systemctl and logger do nothing.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(dirname "$HERE")
RUN=0
FAILED=0

if [ "$(id -u)" = 0 ]; then
    echo "never start this with sudo." >&2
    exit 1
fi

check() {
    RUN=$((RUN + 1))
    if [ "$2" = "$3" ]; then
        printf '  \033[32mok\033[0m   %s\n' "$1"
    else
        FAILED=$((FAILED + 1))
        printf '  \033[31mFAIL\033[0m %s\n       expected [%s]\n       got      [%s]\n' "$1" "$2" "$3"
    fi
}

printf '\n\033[1m== uninstall puts back what was recorded before the install\033[0m\n'
if ! pkg-config --exists phosh-plugins gtk+-3.0 2>/dev/null \
        || ! command -v make >/dev/null || ! command -v cc >/dev/null \
        || ! command -v glib-compile-schemas >/dev/null || ! command -v gsettings >/dev/null \
        || ! python3 -c 'import gi' 2>/dev/null; then
    printf '  \033[33mskipped\033[0m - needs phosh-dev, a compiler, gsettings, glib-compile-schemas and python3-gi\n'
    exit 0
fi
PLUGIN_DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins)

TOP=$(mktemp -d); trap 'rm -rf "$TOP"' EXIT

# phosh's plugin schema, as far as these scripts use it. Compiled into the
# sandbox: schemas from GSETTINGS_SCHEMA_DIR come before the system's.
mkdir -p "$TOP/schemas"
cat > "$TOP/schemas/mobi.phosh.shell.plugins.gschema.xml" <<'XML'
<schemalist>
  <schema id="mobi.phosh.shell.plugins" path="/mobi/phosh/shell/plugins/">
    <key name="status-icons" type="as"><default>[]</default></key>
  </schema>
</schemalist>
XML
glib-compile-schemas "$TOP/schemas"

# One sandbox per case. $1: "bare" (no ~/.local, ~/.cache, nothing in the
# stage) or "lived-in" (what a phone that has been used has).
new_case() {
    WORK="$TOP/$1-$RUN"
    ROOT="$WORK/root"; TESTHOME="$WORK/home"
    mkdir -p "$WORK/bin" "$ROOT" "$TESTHOME/.config/glib-2.0/settings"
    if [ "$2" = lived-in ]; then
        for d in "$PLUGIN_DIR" /etc/systemd/user /usr/local/bin /usr/local/share; do
            mkdir -p "$ROOT$d"
        done
        mkdir -p "$TESTHOME/.cache/other" "$TESTHOME/.local/state/other"
        echo own > "$TESTHOME/.cache/other/file"
    fi
    cat > "$WORK/bin/sudo" <<EOF
#!/bin/bash
case "\$1" in
make) case " \$* " in *" DESTDIR=$ROOT "*) ;; *)
        echo "sudo make without DESTDIR=$ROOT: \$*" >> "$WORK/refused"; exit 97 ;; esac ;;
install|rm|rmdir|mkdir) ;;
*) echo "sudo \$1 is not stood in for: \$*" >> "$WORK/refused"; exit 97 ;;
esac
for a in "\$@"; do
    case "\$a" in
    *=*|-*) ;;
    /*) case "\$a" in "$WORK"/*|"$REPO"/*) ;; *)
            echo "system path: \$*" >> "$WORK/refused"; exit 97 ;; esac ;;
    esac
done
exec "\$@"
EOF
    for stub in systemctl logger; do printf '#!/bin/sh\nexit 0\n' > "$WORK/bin/$stub"; done
    chmod +x "$WORK/bin/"*
}

in_case() {
    env -u XDG_CONFIG_HOME -u XDG_CACHE_HOME -u XDG_STATE_HOME -u XDG_DATA_HOME \
        -u DBUS_SESSION_BUS_ADDRESS \
        HOME="$TESTHOME" PATH="$WORK/bin:$PATH" DESTDIR="$ROOT" \
        GSETTINGS_BACKEND=keyfile GSETTINGS_SCHEMA_DIR="$TOP/schemas" "$@"
}
gs() { local verb=$1; shift; in_case gsettings "$verb" mobi.phosh.shell.plugins status-icons "$@"; }
# What dconf would hold: the key's line in the key file, or nothing at all
# when it has no value - which is not the same as holding the default.
stored() { grep '^status-icons=' "$TESTHOME/.config/glib-2.0/settings/keyfile" 2>/dev/null; }
tree() { (cd "$1" && find . -mindepth 1 -not -path './.config/glib-2.0*' | LC_ALL=C sort | tr '\n' ' '); }
snap() { ROOT_BEFORE=$(tree "$ROOT"); HOME_BEFORE=$(tree "$TESTHOME"); }
same() {
    check "$1: the stage is as before" "$ROOT_BEFORE" "$(tree "$ROOT")"
    check "$1: the home is as before" "$HOME_BEFORE" "$(tree "$TESTHOME")"
    check "$1: no system path outside the stage" "" "$(cat "$WORK/refused" 2>/dev/null)"
}
run() { in_case "$@" > "$WORK/out" 2>&1 || { cat "$WORK/out"; check "$* ran" 0 1; }; }
# The app's switch for the dock, as furios_app does it: our name appended.
app_dock_on() {
    in_case python3 -c '
from gi.repository import Gio
s = Gio.Settings.new("mobi.phosh.shell.plugins")
s.set_strv("status-icons", [n for n in s.get_strv("status-icons") if n != "furios-folder-dock"] + ["furios-folder-dock"])
Gio.Settings.sync()'
    printf '[dock]\none-row=true\n' > "$TESTHOME/.config/furios-folder-dock.conf"
}
everything() {
    run "$REPO/folder-dock/install.sh"
    run "$REPO/lockout-status/install.sh"
}
nothing() {
    run "$REPO/folder-dock/uninstall.sh"
    run "$REPO/lockout-status/uninstall.sh"
}

# 1. A new phone: the key has no value, and there is no ~/.local, ~/.cache or
# plugin directory. Everything on, the dock switched on in the app, the shell
# crashing twice so the guard steps in, then everything off.
new_case unset bare; snap
everything
check "unset: the lockout lists itself" "['furios-lockout']" "$(gs get)"
check "unset: the record says the key had no value" "False" \
    "$(in_case python3 -c 'import json,os;print(json.load(open(os.path.expanduser("~/.local/state/furios-phosh/original.json")))["key"]["set"])')"
app_dock_on
guard="$ROOT/usr/local/libexec/furios-phosh-guard"
in_case sh "$guard" check 2>/dev/null; in_case sh "$guard" check 2>/dev/null
check "unset: the guard took our names out" "@as []" "$(gs get)"
nothing
same "unset"
check "unset: the key has no value again (reset, not a copy of the default)" "" "$(stored)"

# 2. The default written on purpose: pinned before, so pinned after - the old
# uninstaller reset it because it looked like the default.
new_case pinned lived-in; snap
gs set "@as []"
pinned=$(stored)
check "pinned default: the key has a value before" yes "$([ -n "$pinned" ] && echo yes || echo no)"
everything; app_dock_on; nothing
same "pinned default"
check "pinned default: still has a value, and the same" "$pinned" "$(stored)"

# 3. A list of somebody's own, in an order of their own: exactly that back.
new_case own lived-in; snap
gs set "['wifi-hotspot', 'furios-battery-time']"
everything; app_dock_on; nothing
same "own list"
check "own list: back as it was, in its order" "['wifi-hotspot', 'furios-battery-time']" "$(gs get)"

# 4. Changed after us: somebody adds a plugin while ours are installed. Only
# our names come out, and the uninstaller says the list is not the original.
new_case changed lived-in
everything
gs set "['furios-lockout', 'caffeine']"
run "$REPO/folder-dock/uninstall.sh"
in_case "$REPO/lockout-status/uninstall.sh" > "$WORK/out" 2>&1
check "changed since: their plugin stays, ours goes" "['caffeine']" "$(gs get)"
check "changed since: and that is said" yes \
    "$(grep -q 'changed since the install' "$WORK/out" && echo yes || echo no)"

# 5. A second install keeps the first record: installed on a new phone,
# installed again (now with our name in the list), uninstalled - back to no
# value, and no talk of a missing record.
new_case again lived-in; snap
run "$REPO/lockout-status/install.sh"
rec="$TESTHOME/.local/state/furios-phosh/original.json"
first=$(md5sum < "$rec")
run "$REPO/lockout-status/install.sh"
run "$REPO/folder-dock/install.sh"
check "reinstall: the record is not taken again" "$first" "$(md5sum < "$rec")"
nothing
same "reinstall"
check "reinstall: back to no value" "" "$(stored)"

# 6. Nothing recorded (installed before records were kept): the uninstaller
# falls back to comparing with the default and says it is guessing.
new_case legacy lived-in
everything
rm -rf "$TESTHOME/.local/state/furios-phosh"
run "$REPO/folder-dock/uninstall.sh"
in_case "$REPO/lockout-status/uninstall.sh" > "$WORK/out" 2>&1
check "no record: says so" yes "$(grep -q 'no record' "$WORK/out" && echo yes || echo no)"
check "no record: the default list is reset as before" "" "$(stored)"

# 7. Directories that were there stay, even empty: the old uninstaller took
# out /usr/local/libexec and the shell's drop-in directory whenever they were
# empty, whoever had made them.
new_case dirs lived-in
mkdir -p "$ROOT/usr/local/libexec" "$ROOT/etc/systemd/user/mobi.phosh.Shell.service.d"
snap
everything; nothing
same "empty directories that were there"

# 8. Only one plugin goes: the other's name, the guard and the record stay.
new_case partial lived-in
everything; app_dock_on
run "$REPO/lockout-status/uninstall.sh"
guard="$ROOT/usr/local/libexec/furios-phosh-guard"
check "one of two: only its name leaves the list" "['furios-folder-dock']" "$(gs get)"
check "one of two: the guard stays" yes "$([ -e "$guard" ] && echo yes || echo no)"
check "one of two: the record stays" yes \
    "$([ -e "$TESTHOME/.local/state/furios-phosh/original.json" ] && echo yes || echo no)"

echo
if [ "$FAILED" -eq 0 ]; then
    printf '\033[32m%d checks passed\033[0m\n' "$RUN"
else
    printf '\033[31m%d of %d checks failed\033[0m\n' "$FAILED" "$RUN"
fi
exit $((FAILED > 0))
