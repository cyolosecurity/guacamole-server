#!/bin/bash
# Build guacd as APK package with all dependencies bundled
# This script is self-contained and works both locally and in CI.
#
# Usage: ./build-apk.sh [ARCH]
#   ARCH: Target architecture (aarch64 or x86_64). Default: native architecture.
#
# For cross-compilation, ensure QEMU and Docker buildx are set up:
#   docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
#   docker buildx create --use

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GUACAMOLE_SERVER_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="/tmp/cyolo-apk-output"

# Determine target architecture
TARGET_ARCH="${1:-}"
if [ -z "$TARGET_ARCH" ]; then
    # Auto-detect native architecture
    case "$(uname -m)" in
        x86_64)  TARGET_ARCH="x86_64" ;;
        aarch64) TARGET_ARCH="aarch64" ;;
        arm64)   TARGET_ARCH="aarch64" ;;  # macOS reports arm64
        *)       echo "ERROR: Unknown architecture $(uname -m)"; exit 1 ;;
    esac
fi

# Map to Docker platform
case "$TARGET_ARCH" in
    x86_64)  DOCKER_PLATFORM="linux/amd64" ;;
    aarch64) DOCKER_PLATFORM="linux/arm64" ;;
    *)       echo "ERROR: Unsupported architecture $TARGET_ARCH"; exit 1 ;;
esac

echo "Building guacd APK (with bundled dependencies)"
echo "=================================================="
echo "   Target Architecture: $TARGET_ARCH ($DOCKER_PLATFORM)"
echo ""

# Extract version from APKBUILD
echo "Reading version from APKBUILD..."
GUACD_VERSION=$(grep "^pkgver=" "$SCRIPT_DIR/APKBUILD" | cut -d= -f2)
GUACD_RELEASE=$(grep "^pkgrel=" "$SCRIPT_DIR/APKBUILD" | cut -d= -f2)
echo "   guacd Version: $GUACD_VERSION-r$GUACD_RELEASE"
echo ""

# Build guacd using Docker BEFORE starting APK build
DOCKER_IMAGE="cyolo-guacd-builder:$GUACD_VERSION-$GUACD_RELEASE-$TARGET_ARCH"

# Check if Docker image already exists
if ! docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "^$DOCKER_IMAGE\$"; then
    echo "Building guacd Docker image (builder stage only)..."
    echo "   Platform: $DOCKER_PLATFORM"
    echo "   This will compile guacd and all dependencies (~5-10 minutes)"
    cd "$GUACAMOLE_SERVER_DIR"
    # Override PREFIX_DIR so FreeRDP's compiled-in plugin path matches the
    # launcher's runtime path (/host/cyolo/software/guacd is a stable symlink
    # maintained by the launcher pointing to the versioned install directory).
    docker buildx build -t "$DOCKER_IMAGE" \
        --platform "$DOCKER_PLATFORM" \
        --target builder \
        --build-arg ALPINE_BASE_IMAGE=3.18.6 \
        --build-arg PREFIX_DIR=/host/cyolo/software/guacd \
        --load \
        -f Dockerfile \
        . || { echo "ERROR: Docker build failed"; exit 1; }
    echo "Docker build complete"
else
    echo "Docker image already exists: $DOCKER_IMAGE"
fi
echo ""

# Extract artifacts on HOST machine
EXTRACT_DIR="/tmp/guacd-extract-$$"
BUILD_DIR="/tmp/guacd-build-$$"

# Cleanup function that handles root-owned files from Docker
cleanup() {
    # Use Docker to remove files that may be owned by root
    if [ -d "$BUILD_DIR" ] || [ -d "$EXTRACT_DIR" ]; then
        docker run --rm \
            --platform "${DOCKER_PLATFORM:-linux/amd64}" \
            -v "$BUILD_DIR:/build" \
            -v "$EXTRACT_DIR:/extract" \
            alpine:3.18.6 \
            sh -c "rm -rf /build/* /extract/* 2>/dev/null || true"
    fi
    rm -rf "$EXTRACT_DIR" "$BUILD_DIR" 2>/dev/null || true
}
trap cleanup EXIT

echo "Extracting guacd artifacts..."
mkdir -p "$EXTRACT_DIR/bundled-libs"

# Extract build artifacts from the image (PREFIX_DIR=/host/cyolo/software/guacd)
# Rename to "guacamole" locally so the rest of the script and APKBUILD work unchanged.
CONTAINER_ID=$(docker create "$DOCKER_IMAGE")
docker cp "$CONTAINER_ID:/host/cyolo/software/guacd" "$EXTRACT_DIR/guacamole"
docker rm "$CONTAINER_ID" > /dev/null

echo "Artifacts extracted"
echo ""

# Collect ALL shared library dependencies
echo "Collecting shared library dependencies..."
echo "   This will bundle all .so files for portability"

# Start a container with the extracted guacamole mounted
DEPS_CONTAINER=$(docker run -d \
    --platform "$DOCKER_PLATFORM" \
    -v "$EXTRACT_DIR:/extract" \
    alpine:3.18.6 sleep 3600)

# Install runtime packages first (so ldd can resolve all dependencies)
echo "   Installing runtime packages for dependency resolution..."
docker exec "$DEPS_CONTAINER" apk add --no-cache \
    ca-certificates font-noto-cjk ghostscript netcat-openbsd shadow \
    terminus-font ttf-dejavu ttf-liberation util-linux-login \
    cairo openssl1.1-compat libjpeg-turbo libpng libwebp util-linux-misc \
    pango > /dev/null 2>&1

# Collect all dependencies from guacd and its libraries
echo "   Analyzing dependencies with ldd..."
docker exec "$DEPS_CONTAINER" sh -c '
    # Set LD_LIBRARY_PATH so ldd can find libguac and other custom libraries
    export LD_LIBRARY_PATH=/extract/guacamole/lib:/extract/guacamole/lib/freerdp2
    
    # Collect dependencies from guacd binary
    ldd /extract/guacamole/sbin/guacd 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"
    
    # Collect dependencies from all .so files in lib/
    find /extract/guacamole/lib -name "*.so*" -type f 2>/dev/null | while read -r lib; do
        ldd "$lib" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/" || true
    done
' | sort -u > "$EXTRACT_DIR/deps-list.txt"

NUM_DEPS=$(wc -l < "$EXTRACT_DIR/deps-list.txt")
echo "   Found $NUM_DEPS unique shared libraries to bundle"

if [ "$NUM_DEPS" -eq 0 ]; then
    echo "   WARNING: No dependencies found - this might cause runtime errors!"
else
    # Copy all dependencies - dereference symlinks!
    echo "   Copying shared libraries (dereferencing symlinks)..."
    dep_count=0
    while IFS= read -r dep; do
        if [ -n "$dep" ] && [ "$dep" != " " ]; then
            # Copy from container, using basename for destination
            filename=$(basename "$dep")
            # Use cat to dereference symlinks and copy actual file content
            if docker exec "$DEPS_CONTAINER" cat "$dep" > "$EXTRACT_DIR/bundled-libs/$filename" 2>/dev/null; then
                dep_count=$((dep_count + 1))
            else
                echo "   Warning: Failed to copy $dep"
            fi
        fi
    done < "$EXTRACT_DIR/deps-list.txt"
    echo "   Successfully copied $dep_count out of $NUM_DEPS libraries"
fi

# Cleanup deps container
docker stop "$DEPS_CONTAINER" > /dev/null
docker rm "$DEPS_CONTAINER" > /dev/null

BUNDLED_COUNT=$(ls -1 "$EXTRACT_DIR/bundled-libs" 2>/dev/null | wc -l)
echo "   Bundled $BUNDLED_COUNT library files"

if [ "$BUNDLED_COUNT" -gt 0 ]; then
    echo "   Sample of bundled libraries:"
    ls -1 "$EXTRACT_DIR/bundled-libs" | head -10
fi

echo "Dependencies collected"
echo ""

# Prepare build directory with all sources
echo "Preparing APK build directory..."
mkdir -p "$BUILD_DIR"

# Create tarballs for APK sources
tar -czf "$BUILD_DIR/guacamole.tar.gz" -C "$EXTRACT_DIR" guacamole
tar -czf "$BUILD_DIR/bundled-libs.tar.gz" -C "$EXTRACT_DIR" bundled-libs

# Copy APKBUILD and entrypoint
cp "$SCRIPT_DIR/APKBUILD" "$BUILD_DIR/"
cp "$SCRIPT_DIR/entrypoint.sh" "$BUILD_DIR/"

echo "Build directory ready"
echo ""

# Use TARGET_ARCH for APK naming
ARCH="$TARGET_ARCH"

# Build the APK using ephemeral container (self-contained, no external dependencies)
echo "Building APK package..."
mkdir -p "$OUTPUT_DIR"

docker run --rm \
    --platform "$DOCKER_PLATFORM" \
    -v "$BUILD_DIR:/build" \
    -v "$OUTPUT_DIR:/output" \
    -w /build \
    alpine:3.18.6 \
    sh -c "
        set -e
        
        echo 'Installing build tools...'
        apk add --no-cache alpine-sdk abuild > /dev/null
        
        echo 'Creating builder user...'
        adduser -D builder
        addgroup builder abuild
        mkdir -p /home/builder/.abuild
        chown -R builder:builder /home/builder
        
        echo 'Setting up signing keys...'
        su builder -c 'abuild-keygen -a -n'
        cp /home/builder/.abuild/*.rsa.pub /etc/apk/keys/
        
        echo 'Setting permissions...'
        chown -R builder:builder /build /output
        
        echo 'Generating checksums and building APK...'
        su builder -c 'cd /build && abuild checksum && abuild -r -F'
        
        echo 'Copying APK to output...'
        find /home/builder/packages -name '*.apk' -exec cp {} /output/ \;
        
        echo 'Fixing output permissions...'
        # Make output files world-writable so host user can rename/move them
        chmod -R 777 /output
        
        echo 'Build complete!'
    " || { echo "ERROR: APK build failed"; exit 1; }

echo "APK built successfully"
echo ""

# Locate and rename the APK
echo "Finalizing APK..."

# Find the built APK
BUILT_APK=$(find "$OUTPUT_DIR" -name "guacd-*.apk" -type f | head -1)
if [ -z "$BUILT_APK" ]; then
    echo "ERROR: APK file not found in $OUTPUT_DIR"
    ls -la "$OUTPUT_DIR"
    exit 1
fi

# Compute SHA256 hash (first 16 chars, same as IDAC)
APK_HASH=$(sha256sum "$BUILT_APK" | cut -c1-16)

# Final APK name matches convention: {component}-{version}-r{release}-{arch}-{hash}.apk
FINAL_APK_NAME="guacd-${GUACD_VERSION}-r${GUACD_RELEASE}-${ARCH}-${APK_HASH}.apk"

# Rename to final name
mv "$BUILT_APK" "$OUTPUT_DIR/$FINAL_APK_NAME"

echo "   APK: $FINAL_APK_NAME"
echo "   Location: $OUTPUT_DIR/$FINAL_APK_NAME"
echo ""

# Display APK info
echo "APK Information:"
echo "   Size: $(ls -lh "$OUTPUT_DIR/$FINAL_APK_NAME" | awk '{print $5}')"
echo "   Hash: $APK_HASH"
echo "   Bundled libraries: $BUNDLED_COUNT shared objects"
echo ""
echo "   Contents (first 25 files):"
tar -tzf "$OUTPUT_DIR/$FINAL_APK_NAME" | head -25
echo "   ... (truncated)"
echo ""

echo "=================================================="
echo "Build complete!"
echo ""
echo "APK package ready:"
echo "  $OUTPUT_DIR/$FINAL_APK_NAME"
echo ""
echo "This is a self-contained package with all dependencies bundled."
echo ""
echo "Next steps:"
echo "  1. Test extraction:"
echo "     mkdir -p /tmp/test-guacd"
echo "     tar -xzf $OUTPUT_DIR/$FINAL_APK_NAME -C /tmp/test-guacd"
echo "     ls -la /tmp/test-guacd/software/guacd/$GUACD_VERSION/"
echo ""
echo "  2. Copy to cyolauncher cache:"
echo "     cp $OUTPUT_DIR/$FINAL_APK_NAME /tmp/cyolo-dev/launcher/cache/"
echo ""
