#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
#
# Installs the guard that takes our plugins out of phosh when it keeps dying.
# Run WITHOUT sudo; the two lines that need root ask for it. Takes effect at
# the next start of the shell.
set -euo pipefail
if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo - only the install step needs root." >&2
    exit 1
fi
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sudo install -Dm755 "$SRC/furios-phosh-guard" /usr/local/libexec/furios-phosh-guard
sudo install -Dm644 "$SRC/50-furios-phosh-guard.conf" \
    /etc/systemd/user/mobi.phosh.Shell.service.d/50-furios-phosh-guard.conf
systemctl --user daemon-reload 2>/dev/null || true
echo "phosh guard installed - active from the next start of the shell."
