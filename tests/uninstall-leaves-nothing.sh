#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# The rule for every uninstaller here: afterwards the phone is as it shipped,
# so that the next install.sh behaves exactly as on a new one. NEVER with sudo.
#
# Checked from the outside, not by reading uninstall.sh against install.sh:
# both installers run for real, into a staged root (DESTDIR) and a home of
# their own, with a sudo, a gsettings and a systemctl that only stand in.
# Then the plugins do what they do at runtime - the guard runs as the shell
# unit would run it, and every file the C code builds under a user directory
# is put where the code says - and then both uninstallers run. What is left
# in the staged root and the home, compared with what was there before,
# is the gap: nothing has to be listed here for it to be found.
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

printf '\n\033[1m== uninstall leaves the phone as it shipped\033[0m\n'
if ! pkg-config --exists phosh-plugins gtk+-3.0 2>/dev/null \
        || ! command -v make >/dev/null || ! command -v cc >/dev/null; then
    printf '  \033[33mskipped\033[0m - the installers build the plugins, and there is nothing to build with\n'
    exit 0
fi
PLUGIN_DIR=$(pkg-config --variable=status_icons_plugins_dir phosh-plugins)

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
ROOT="$WORK/root"
TESTHOME="$WORK/home"
mkdir -p "$WORK/bin" "$WORK/gs" "$TESTHOME"

# What a new phone has, as far as these installers are concerned: phosh's
# plugin directory, the user units' directory in /etc, and what Debian's
# base-files creates under /usr/local. /usr/local/libexec is not in that list.
for d in "$PLUGIN_DIR" /etc/systemd/user /usr/local/bin /usr/local/etc \
         /usr/local/include /usr/local/lib /usr/local/sbin /usr/local/share \
         /usr/local/src; do
    mkdir -p "$ROOT$d"
done
# Somebody's own files in the home, which have to survive.
mkdir -p "$TESTHOME/.config/gtk-3.0" "$TESTHOME/.cache/other" "$TESTHOME/.local/state/other"
echo "own" > "$TESTHOME/.config/gtk-3.0/gtk.css"
echo "own" > "$TESTHOME/.cache/other/file"

# sudo runs the command as it is, but refuses any path outside the stage:
# should one of the scripts ever name a system path the stage does not
# reach, the test fails here instead of changing this phone.
cat > "$WORK/bin/sudo" <<EOF
#!/bin/bash
case "\$1" in
make)
    case " \$* " in *" DESTDIR=$ROOT "*) ;; *)
        echo "sudo make without DESTDIR=$ROOT: \$*" >> "$WORK/refused"; exit 97 ;;
    esac ;;
install|rm|rmdir|mkdir) ;;
*) echo "sudo \$1 is not stood in for: \$*" >> "$WORK/refused"; exit 97 ;;
esac
for a in "\$@"; do
    case "\$a" in
    *=*|-*) ;;
    /*) case "\$a" in "$WORK"/*|"$REPO"/*) ;; *)
            echo "system path: \$*" >> "$WORK/refused"; exit 97 ;;
        esac ;;
    esac
done
exec "\$@"
EOF
# The daemon-reload after install and uninstall, and the guard's line in the
# journal: neither may reach this phone's.
for stub in systemctl logger; do
    printf '#!/bin/sh\nexit 0\n' > "$WORK/bin/$stub"
done
# One file per key; no file is no value in dconf, which is what a new phone
# has. The memory backend answers the schema default, as the real one does.
cat > "$WORK/bin/gsettings" <<EOF
#!/bin/sh
f="$WORK/gs/\$3"
[ "\$2" = mobi.phosh.shell.plugins ] && [ "\$3" = status-icons ] || exit 1
case "\$1" in
get) if [ "\${GSETTINGS_BACKEND:-}" = memory ] || [ ! -e "\$f" ]; then
         echo "@as []"
     else cat "\$f"; fi ;;
set) printf '%s\n' "\$4" > "\$f" ;;
reset) rm -f "\$f" ;;
*) exit 1 ;;
esac
EOF
chmod +x "$WORK/bin/"*

tree() { (cd "$1" && find . -mindepth 1 | LC_ALL=C sort | tr '\n' ' '); }
stage() {
    env -u XDG_CONFIG_HOME -u XDG_CACHE_HOME -u XDG_STATE_HOME -u XDG_DATA_HOME \
        HOME="$TESTHOME" PATH="$WORK/bin:$PATH" DESTDIR="$ROOT" "$@"
}
ROOT_BEFORE=$(tree "$ROOT")
HOME_BEFORE=$(tree "$TESTHOME")

# A list with somebody else's plugin in it, as the user set it.
printf '%s\n' "['wifi-hotspot']" > "$WORK/gs/status-icons"

stage "$REPO/folder-dock/install.sh" >/dev/null 2>"$WORK/err" \
    || { cat "$WORK/err"; check "folder-dock/install.sh ran" 0 1; }
stage "$REPO/lockout-status/install.sh" >/dev/null 2>"$WORK/err" \
    || { cat "$WORK/err"; check "lockout-status/install.sh ran" 0 1; }
check "the installers put something into the stage" yes \
    "$([ "$(tree "$ROOT")" != "$ROOT_BEFORE" ] && echo yes || echo no)"

# At runtime. The guard, as the shell unit runs it: twice within its window
# is a shell that did not survive, and it keeps what it took out.
guard="$ROOT/usr/local/libexec/furios-phosh-guard"
stage sh "$guard" check 2>/dev/null
stage sh "$guard" check 2>/dev/null
check "the guard kept state in the home" yes \
    "$([ -d "$TESTHOME/.local/state/furios-phosh-guard" ] && echo yes || echo no)"

# Every file the plugins' C code builds under a user directory, where the
# code puts it - read from the source, so a new one is covered without
# anybody remembering to add it. Files the code only reads, and that belong
# to somebody else, are named here with the reason.
read_only="lockout-status.c:STATE_REL"   # the PAM module's, furios_security
for c in "$REPO"/*/*.c; do
    grep -oE 'g_build_filename \(g_get_user_(config|cache|state|data)_dir \(\), [A-Z_]+' "$c" \
    | while read -r _ dir _ macro; do
        dir=${dir#(g_get_user_}; dir=${dir%_dir}
        case " $read_only " in *" $(basename "$c"):$macro "*) continue ;; esac
        name=$(sed -n "s/^#define $macro[[:space:]]*\"\\(.*\\)\"/\\1/p" "$c")
        case "$dir" in
        config) base="$TESTHOME/.config" ;;
        cache) base="$TESTHOME/.cache" ;;
        state) base="$TESTHOME/.local/state" ;;
        data) base="$TESTHOME/.local/share" ;;
        esac
        mkdir -p "$(dirname "$base/$name")"
        printf 'x\n' > "$base/$name"
        echo "$base/$name" >> "$WORK/planted"
    done
done
check "files the plugins write were found in the source" yes \
    "$([ -s "$WORK/planted" ] && echo yes || echo no)"

stage "$REPO/folder-dock/uninstall.sh" >/dev/null 2>"$WORK/err" \
    || { cat "$WORK/err"; check "folder-dock/uninstall.sh ran" 0 1; }
check "the guard stays while a plugin of ours is left" yes \
    "$([ -e "$guard" ] && echo yes || echo no)"
stage "$REPO/lockout-status/uninstall.sh" >/dev/null 2>"$WORK/err" \
    || { cat "$WORK/err"; check "lockout-status/uninstall.sh ran" 0 1; }

check "no system path outside the stage was touched" "" "$(cat "$WORK/refused" 2>/dev/null)"
check "the staged root is as it shipped" "$ROOT_BEFORE" "$(tree "$ROOT")"
check "the home holds only what was there before" "$HOME_BEFORE" "$(tree "$TESTHOME")"
check "somebody else's plugin is still listed" "['wifi-hotspot']" "$(cat "$WORK/gs/status-icons" 2>/dev/null)"

# And from a new phone: no value in dconf before, so none after either.
rm -f "$WORK/gs/status-icons"
stage "$REPO/lockout-status/install.sh" >/dev/null 2>&1
check "the lockout installer lists itself" "['furios-lockout']" "$(cat "$WORK/gs/status-icons" 2>/dev/null)"
stage "$REPO/lockout-status/uninstall.sh" >/dev/null 2>&1
check "a list back at its default is reset, not written" no \
    "$([ -e "$WORK/gs/status-icons" ] && echo yes || echo no)"
check "and the stage is empty again" "$ROOT_BEFORE" "$(tree "$ROOT")"

echo
if [ "$FAILED" -eq 0 ]; then
    printf '\033[32m%d checks passed\033[0m\n' "$RUN"
else
    printf '\033[31m%d of %d checks failed\033[0m\n' "$FAILED" "$RUN"
fi
exit $((FAILED > 0))
