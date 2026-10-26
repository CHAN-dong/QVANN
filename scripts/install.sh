#!/usr/bin/env bash
set -euo pipefail
# Debian/Ubuntu CPU build prerequisites. Run explicitly on a fresh system.
SUDO=()
if [[ "$EUID" -ne 0 ]]; then SUDO=(sudo); fi
"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y build-essential git cmake libssl-dev libopenblas-dev \
    python3 python3-venv
