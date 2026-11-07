#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

bash ./build.sh

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "[WARN] No graphical DISPLAY was detected." >&2
    echo "Run this from a desktop session, or enable WSLg." >&2
fi

exec java -cp out ticketslots.TicketClientApp
