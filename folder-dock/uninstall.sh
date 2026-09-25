#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Takes the status icon out of phosh's plugin directory, and out of the
# setting that lists it. Run WITHOUT sudo.
set -uo pipefail

if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo." >&2
    exit 1
fi

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN="furios-folder-dock"
KEY="mobi.phosh.shell.plugins status-icons"

# The setting first, and only our own entry in it: a shell that goes on being
# told to load a plugin which is no longer there logs a warning on every
# start, and somebody else's plugin in the same list is none of our business.
if command -v gsettings >/dev/null; then
    current=$(gsettings get $KEY 2>/dev/null || echo "@as []")
    if [ "$current" != "${current/$PLUGIN/}" ]; then
        python3 - "$PLUGIN" <<'PY' || true
import subprocess, sys, ast
key = ["mobi.phosh.shell.plugins", "status-icons"]
out = subprocess.run(["gsettings", "get"] + key, capture_output=True, text=True)
text = out.stdout.strip()
if text.startswith("@as "):
    text = text[4:]
try:
    names = [n for n in ast.literal_eval(text) if n != sys.argv[1]]
except (ValueError, SyntaxError):
    sys.exit(0)
subprocess.run(["gsettings", "set"] + key + [str(names)], check=False)
PY
    fi
fi

sudo make -C "$SRC" uninstall

echo "Removed. The shell drops it at its next start."
