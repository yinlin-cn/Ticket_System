#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_USER="${DB_USER:-chat}"
DB_PASSWORD="${DB_PASSWORD:-chat123}"
DB_NAME="${DB_NAME:-chat}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
TICKET_TYPE="${TICKET_TYPE:-}"

# 不指定 TICKET_TYPE 时同步全部票种；指定则只同步那一个。
if [[ -n "${TICKET_TYPE}" ]]; then
    types="${TICKET_TYPE}"
else
    types="$(mariadb -N -B -u"${DB_USER}" -p"${DB_PASSWORD}" "${DB_NAME}" \
        -e "SELECT ticket_type_id FROM ticket_inventory ORDER BY ticket_type_id")"
fi

if [[ -z "${types}" ]]; then
    echo "no ticket types found" >&2
    exit 1
fi

for type in ${types}; do
    stock="$(mariadb -N -B -u"${DB_USER}" -p"${DB_PASSWORD}" "${DB_NAME}" \
        -e "SELECT remaining_qty FROM ticket_inventory WHERE ticket_type_id = ${type}")"
    if [[ -z "${stock}" ]]; then
        echo "ticket type ${type} not found" >&2
        exit 1
    fi
    redis-cli -h "${REDIS_HOST}" -p "${REDIS_PORT}" \
        SET "ticket:stock:${type}" "${stock}" >/dev/null
    echo "redis initialized: ticket:stock:${type}=${stock}"
done

redis-cli -h "${REDIS_HOST}" -p "${REDIS_PORT}" \
    --scan --pattern 'ticket:idem:*' \
    | xargs -r redis-cli -h "${REDIS_HOST}" -p "${REDIS_PORT}" DEL >/dev/null
