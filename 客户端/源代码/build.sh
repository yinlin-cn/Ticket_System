#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if ! command -v java >/dev/null 2>&1; then
    echo "[ERROR] java was not found. Install JDK 17 or newer." >&2
    exit 1
fi

if ! command -v javac >/dev/null 2>&1; then
    echo "[ERROR] javac was not found. Install a JDK, not only a JRE." >&2
    exit 1
fi

echo "[1/3] Java runtime:"
java -version
echo "[2/3] Java compiler:"
javac -version

rm -rf out
mkdir -p out

echo "[3/3] Compiling sources..."
javac --release 17 -encoding UTF-8 -d out \
    src/ticketslots/*.java \
    src/ticketslots/tasks/*.java \
    src/ticketslots/ui/*.java \
    src/ticketslots/model/*.java

echo
echo "Build complete. Launch with ./run.sh."
