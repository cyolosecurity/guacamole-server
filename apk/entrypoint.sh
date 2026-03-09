#!/bin/sh

# entrypoint.sh - Runtime wrapper for guacd
# This script sets up the environment and starts guacd with proper configuration

# Resolve installation directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$(dirname "$SCRIPT_DIR")"

# Set library path for bundled dependencies
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib:${LD_LIBRARY_PATH}"
export LC_ALL=C.UTF-8

# Configure fontconfig to use bundled fonts (needed for SSH/telnet terminal rendering).
# Generate a minimal config at runtime so the font directory path is always correct.
if [ -d "${INSTALL_DIR}/share/fonts" ] && [ ! -f /tmp/guacd-fonts.conf ]; then
    cat > /tmp/guacd-fonts.conf <<FONTCONF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
    <dir>${INSTALL_DIR}/share/fonts</dir>
    <match target="pattern">
        <edit name="family" mode="append_last"><string>DejaVu Sans Mono</string></edit>
    </match>
</fontconfig>
FONTCONF
fi
export FONTCONFIG_FILE="${FONTCONFIG_FILE:-/tmp/guacd-fonts.conf}"

# Default configuration
GUACD_LOG_LEVEL="${GUACD_LOG_LEVEL:-info}"
GUAC_LISTEN_PORT="${GUAC_LISTEN_PORT:-4822}"

echo "Starting guacd..."
echo "  Listen Port: ${GUAC_LISTEN_PORT}"
echo "  Log Level: ${GUACD_LOG_LEVEL}"
echo "  Library Path: ${LD_LIBRARY_PATH}"

# Function to handle signals and propagate them to child processes
handle_signal() {
    echo "Received $1, forwarding to guacd (PID: $child_pid)"
    kill -s "$1" "$child_pid" 2>/dev/null
}

# Trap signals and forward them to the handle_signal function
trap 'handle_signal TERM' TERM
trap 'handle_signal INT' INT

# Start guacd in background
"${INSTALL_DIR}/sbin/guacd" \
    -b 0.0.0.0 \
    -l "${GUAC_LISTEN_PORT}" \
    -L "${GUACD_LOG_LEVEL}" \
    -f &

child_pid=$!
echo "guacd started with PID: $child_pid"

# Wait for guacd to exit and capture its exit code
wait "$child_pid"
exit_code=$?

echo "guacd exited with code: $exit_code"

# Give cleanup time to finish
sleep 1

# Exit with guacd's exit code
exit $exit_code

