#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_USER="${DB_USER:-chat}"
DB_PASSWORD="${DB_PASSWORD:-chat123}"
DB_NAME="${DB_NAME:-chat}"
DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-3306}"

if ! command -v mariadb >/dev/null 2>&1; then
    echo "mariadb client was not found. Run scripts/install_deps.sh first." >&2
    exit 1
fi

if [[ ! "${DB_USER}" =~ ^[A-Za-z0-9_]+$ || ! "${DB_NAME}" =~ ^[A-Za-z0-9_]+$ ]]; then
    echo "DB_USER and DB_NAME may contain only letters, digits and underscores." >&2
    exit 1
fi

if [[ -z "${DB_PASSWORD}" || "${DB_PASSWORD}" == *"'"* || "${DB_PASSWORD}" == *$'\n'* ]]; then
    echo "DB_PASSWORD must be non-empty and may not contain single quotes or newlines." >&2
    exit 1
fi

if [[ ${EUID} -eq 0 ]]; then
    ADMIN=(mariadb)
elif command -v sudo >/dev/null 2>&1; then
    ADMIN=(sudo mariadb)
else
    echo "Administrator access is required. Run this script as root or install sudo." >&2
    exit 1
fi

# Ubuntu/WSL MariaDB often creates distribution accounts with unix_socket
# authentication. Create the accounts and force password authentication over TCP.
"${ADMIN[@]}" <<SQL
CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASSWORD}';
ALTER USER '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASSWORD}';
CREATE USER IF NOT EXISTS '${DB_USER}'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD}';
ALTER USER '${DB_USER}'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD}';

GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'localhost';
GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

mariadb --protocol=TCP \
    -h"${DB_HOST}" -P"${DB_PORT}" \
    -u"${DB_USER}" -p"${DB_PASSWORD}" "${DB_NAME}" \
    < "${ROOT}/db/init.sql"

echo "database initialized"
