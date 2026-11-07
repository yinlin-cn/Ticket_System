#!/usr/bin/env bash
set -euo pipefail

ulimit -n 65535

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "${ROOT}/build/ticket_server"
