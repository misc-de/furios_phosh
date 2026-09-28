#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2026 misc-de
# SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$(id -u)" = 0 ]; then
    echo "Please run WITHOUT sudo." >&2
    exit 1
fi
sudo rm -f /usr/local/libexec/furios-phosh-guard \
    /etc/systemd/user/mobi.phosh.Shell.service.d/50-furios-phosh-guard.conf
sudo rmdir /etc/systemd/user/mobi.phosh.Shell.service.d 2>/dev/null || true
systemctl --user daemon-reload 2>/dev/null || true
echo "phosh guard removed."
