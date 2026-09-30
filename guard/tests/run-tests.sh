#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# The guard against a shell that keeps dying. NEVER with sudo: gsettings is a
# stand-in here, and nothing touches the running shell's settings.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
GUARD="$(dirname "$HERE")/furios-phosh-guard"
RUN=0
FAILED=0

if [ "$(id -u)" = 0 ]; then
    echo "never start run-tests.sh with sudo." >&2
    exit 1
fi

check() {
    RUN=$((RUN + 1))
    if [ "$2" = "$3" ]; then
        printf '  \033[32mok\033[0m   %s\n' "$1"
    else
        FAILED=$((FAILED + 1))
        printf '  \033[31mFAIL\033[0m %s\n       expected [%s], got [%s]\n' "$1" "$2" "$3"
    fi
}

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
# A gsettings that keeps one value per key in a file, and knows only
# status-icons - "plugins" is a key some phosh versions have and some do not.
cat > "$WORK/gsettings" <<EOF
#!/bin/sh
f="$WORK/store.\$3"
case "\$1" in
get) [ "\$3" = status-icons ] || exit 1; cat "\$f" ;;
set) [ "\$3" = status-icons ] || exit 1; printf '%s\n' "\$4" > "\$f" ;;
esac
EOF
chmod +x "$WORK/gsettings"

guard() {
    env FURIOS_PHOSH_GUARD_STATE="$WORK/state" FURIOS_PHOSH_GUARD_GSETTINGS="$WORK/gsettings" \
        FURIOS_PHOSH_GUARD_BOOT_ID="${BOOT:-boot-a}" FURIOS_PHOSH_GUARD_WINDOW="${WINDOW:-60}" \
        sh "$GUARD" "$@" 2>/dev/null
}
icons() { cat "$WORK/store.status-icons"; }

printf '\n\033[1m== the phosh guard\033[0m\n'
LIST="['furios-lockout', 'wifi-hotspot', 'furios-battery-time']"
printf '%s\n' "$LIST" > "$WORK/store.status-icons"

guard check
check "a first start changes nothing" "$LIST" "$(icons)"

guard check
check "a second start within the window takes only our plugins out" \
      "['wifi-hotspot']" "$(icons)"
check "and says so" yes "$(guard status | grep -q 'disabled since' && echo yes || echo no)"
check "what was chosen is kept" "$LIST" "$(cat "$WORK/state/status-icons")"

guard check
check "a third start keeps the first list, not the emptied one" \
      "$LIST" "$(cat "$WORK/state/status-icons")"

guard restore >/dev/null
check "restore puts the list back" "$LIST" "$(icons)"
check "and clears the mark" yes "$(guard status | grep -q 'not triggered' && echo yes || echo no)"

# restore undoes only the guard's own change. A list somebody changed after
# the guard stepped in is theirs: it stays, and the saved one is kept and
# shown, to be put back by hand.
printf '%s\n' "$LIST" > "$WORK/store.status-icons"
rm -rf "$WORK/state"
guard check; guard check
printf "['wifi-hotspot', 'caffeine']\n" > "$WORK/store.status-icons"
out=$(guard restore)
check "restore leaves a list changed since alone" "['wifi-hotspot', 'caffeine']" "$(icons)"
check "and says so" yes "$(grep -q 'changed since' <<<"$out" && echo yes || echo no)"
check "and keeps what it saved" "$LIST" "$(cat "$WORK/state/status-icons")"
check "and still reports itself triggered" yes \
    "$(guard status | grep -q 'disabled since' && echo yes || echo no)"
printf "['wifi-hotspot']\n" > "$WORK/store.status-icons"
guard restore >/dev/null
check "back to what the guard wrote, restore goes through" "$LIST" "$(icons)"

rm -rf "$WORK/state"
guard check
BOOT=boot-b guard check
check "a quick reboot is not a crash" "$LIST" "$(icons)"

rm -rf "$WORK/state"
guard check
WINDOW=0 guard check
check "starts further apart than the window are not a crash" "$LIST" "$(icons)"

rm -rf "$WORK/state"
printf "['furios-lockout']\n" > "$WORK/store.status-icons"
guard check; guard check
check "a list of only ours becomes an empty typed list" "@as []" "$(icons)"

rm -rf "$WORK/state"
printf "['wifi-hotspot']\n" > "$WORK/store.status-icons"
guard check; guard check
check "without our plugins nothing is touched" "['wifi-hotspot']" "$(icons)"
check "and nothing is marked" yes "$(guard status | grep -q 'not triggered' && echo yes || echo no)"

printf 'garbage\n' > "$WORK/state/last-start"
check_rc=0; guard check || check_rc=$?
check "a garbled record is not an error" 0 "$check_rc"

printf '\n  %d checks, %d failed\n' "$RUN" "$FAILED"
[ "$FAILED" -eq 0 ]
