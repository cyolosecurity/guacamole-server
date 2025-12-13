#!/bin/sh
set -e

# entrypoint.sh - Runtime wrapper for guacd
# This script sets up the environment and starts guacd with proper configuration

# Resolve installation directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$(dirname "$SCRIPT_DIR")"

# Set library path for bundled dependencies
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib:${LD_LIBRARY_PATH}"
export LC_ALL=C.UTF-8

# Default configuration
GUACD_LOG_LEVEL="${GUACD_LOG_LEVEL:-info}"
GUAC_LISTEN_PORT="${GUAC_LISTEN_PORT:-4822}"

echo "Starting guacd..."
echo "  Listen Port: ${GUAC_LISTEN_PORT}"
echo "  Log Level: ${GUACD_LOG_LEVEL}"
echo "  Library Path: ${LD_LIBRARY_PATH}"

# Signal handling - forward signals to child process
handle_signal() {
    echo "Received signal, forwarding to guacd (PID: $child_pid)"
    kill -TERM "$child_pid" 2>/dev/null
}

trap 'handle_signal' TERM INT

# Start guacd in background
"${INSTALL_DIR}/sbin/guacd" \
    -b 0.0.0.0 \
    -l "${GUAC_LISTEN_PORT}" \
    -L "${GUACD_LOG_LEVEL}" \
    -f &

child_pid=$!
echo "guacd started with PID: $child_pid"

# Wait for guacd to exit
wait "$child_pid"
exit_code=$?

echo "guacd exited with code: $exit_code"

# Give bugsnag/cleanup time to finish
sleep 1

exit $exit_code

