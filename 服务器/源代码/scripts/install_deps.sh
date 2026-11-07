#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This script supports Debian, Ubuntu and WSL distributions using apt." >&2
    echo "Install the equivalent development packages with your package manager." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    SUDO=()
elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
else
    echo "Root permission is required. Run this script as root or install sudo." >&2
    exit 1
fi

PACKAGES=(
    build-essential
    cmake
    pkg-config
    libssl-dev
    libmariadb-dev
    libhiredis-dev
    mariadb-server
    mariadb-client
    redis-server
    redis-tools
)

echo "Installing ticket server dependencies..."
"${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get update
"${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install \
    -y --no-install-recommends "${PACKAGES[@]}"

missing=()

command -v g++ >/dev/null 2>&1 || missing+=("g++")
command -v cmake >/dev/null 2>&1 || missing+=("cmake")
command -v mariadb >/dev/null 2>&1 || missing+=("mariadb client")
command -v redis-cli >/dev/null 2>&1 || missing+=("redis-cli")

[[ -f /usr/include/openssl/ssl.h ]] || missing+=("OpenSSL headers")
[[ -f /usr/include/hiredis/hiredis.h ]] || missing+=("hiredis headers")

mariadb_include="$(find /usr/include -path '*/mariadb/mysql.h' -print -quit 2>/dev/null || true)"
[[ -n "${mariadb_include}" ]] || missing+=("MariaDB Connector/C headers")

if (( ${#missing[@]} > 0 )); then
    echo "Dependency installation finished, but these items were not found:" >&2
    printf '  - %s\n' "${missing[@]}" >&2
    exit 1
fi

cat <<'EOF'

Dependencies installed and verified.

Start the local services:
  sudo service mariadb start
  sudo service redis-server start

Then run:
  bash scripts/init_db.sh
  bash scripts/init_redis.sh
  bash scripts/build.sh
  bash scripts/run_server.sh
EOF
