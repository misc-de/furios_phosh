#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# The tests of every subproject. NEVER start it with sudo.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FAILED=0
FOUND=0

if [ "$(id -u)" = 0 ]; then
    echo "never start run-tests.sh with sudo." >&2
    exit 1
fi

for suite in "$HERE"/*/tests/run-tests.sh; do
    [ -x "$suite" ] || continue
    project=$(basename "$(dirname "$(dirname "$suite")")")
    FOUND=$((FOUND + 1))
    printf '\n\033[1m#### %s\033[0m\n' "$project"
    if bash "$suite"; then :; else FAILED=$((FAILED + 1)); fi
done

echo
if [ "$FOUND" -eq 0 ]; then
    # Otherwise an empty collection cheerfully reports success.
    printf '\033[31mno test suite found\033[0m\n'
    exit 1
fi
if [ "$FAILED" -eq 0 ]; then
    printf '\033[32mall %d projects passed\033[0m\n' "$FOUND"
else
    printf '\033[31m%d of %d projects failed\033[0m\n' "$FAILED" "$FOUND"
fi
exit $((FAILED > 0))
