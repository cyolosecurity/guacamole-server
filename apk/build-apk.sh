#!/bin/bash
# Build guacd as APK package with all dependencies bundled

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GUACAMOLE_SERVER_DIR="$(dirname "$SCRIPT_DIR")"
APK_BUILDER_DIR="$GUACAMOLE_SERVER_DIR/../apk-builder"
OUTPUT_DIR="/tmp/cyolo-apk-output"

echo "🏗️  Building guacd APK (with bundled dependencies)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Extract version from APKBUILD
echo "📋 Reading version from APKBUILD..."
GUACD_VERSION=$(grep "^pkgver=" "$SCRIPT_DIR/APKBUILD" | cut -d= -f2)
GUACD_RELEASE=$(grep "^pkgrel=" "$SCRIPT_DIR/APKBUILD" | cut -d= -f2)
echo "   guacd Version: $GUACD_VERSION-r$GUACD_RELEASE"
echo ""

# Build guacd using Docker BEFORE starting APK build
DOCKER_IMAGE="cyolo-guacd-builder:$GUACD_VERSION-$GUACD_RELEASE"

# Check if Docker image already exists
if ! docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "^$DOCKER_IMAGE\$"; then
    echo "🐳 Building guacd Docker image (builder stage only)..."
    echo "   This will compile guacd and all dependencies (~5-10 minutes)"
    cd "$GUACAMOLE_SERVER_DIR"
    docker build -t "$DOCKER_IMAGE" \
        --target builder \
        --build-arg ALPINE_BASE_IMAGE=3.18.6 \
        -f Dockerfile \
        . || { echo "❌ Docker build failed"; exit 1; }
    echo "✅ Docker build complete"
else
    echo "✅ Docker image already exists: $DOCKER_IMAGE"
fi
echo ""

# Extract artifacts on HOST machine
EXTRACT_DIR="/tmp/guacd-extract-$$"
trap "rm -rf $EXTRACT_DIR" EXIT

echo "📦 Extracting guacd artifacts..."
mkdir -p "$EXTRACT_DIR/bundled-libs"

# Extract /opt/guacamole from the image
CONTAINER_ID=$(docker create "$DOCKER_IMAGE")
docker cp "$CONTAINER_ID:/opt/guacamole" "$EXTRACT_DIR/"
docker rm "$CONTAINER_ID" > /dev/null

echo "✅ Artifacts extracted"
echo ""

# Collect ALL shared library dependencies
echo "📚 Collecting shared library dependencies..."
echo "   This will bundle all .so files for portability"

# Start a container with the extracted guacamole mounted
DEPS_CONTAINER=$(docker run -d \
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
    echo "   ⚠️  Warning: No dependencies found - this might cause runtime errors!"
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

# Cleanup container
docker stop "$DEPS_CONTAINER" > /dev/null
docker rm "$DEPS_CONTAINER" > /dev/null

BUNDLED_COUNT=$(ls -1 "$EXTRACT_DIR/bundled-libs" 2>/dev/null | wc -l)
echo "   Bundled $BUNDLED_COUNT library files"

if [ "$BUNDLED_COUNT" -gt 0 ]; then
    echo "   Sample of bundled libraries:"
    ls -1 "$EXTRACT_DIR/bundled-libs" | head -10
fi

echo "✅ Dependencies collected"
echo ""

# Start APK builder container if not running
if ! docker ps --filter name=cyolo-apk-builder --format '{{.Names}}' | grep -q cyolo-apk-builder; then
    echo "🐳 Starting APK builder container..."
    cd "$APK_BUILDER_DIR"
    docker compose up -d
    sleep 2
fi

echo "✅ APK builder ready"
echo ""

# Create tarballs for APK sources
echo "📁 Preparing APK sources..."
tar -czf /tmp/guacamole.tar.gz -C "$EXTRACT_DIR" guacamole
tar -czf /tmp/bundled-libs.tar.gz -C "$EXTRACT_DIR" bundled-libs

docker exec -u root cyolo-apk-builder rm -rf /tmp/guacd-build
docker exec -u root cyolo-apk-builder mkdir -p /tmp/guacd-build

docker cp /tmp/guacamole.tar.gz cyolo-apk-builder:/tmp/guacd-build/
docker cp /tmp/bundled-libs.tar.gz cyolo-apk-builder:/tmp/guacd-build/
docker cp "$SCRIPT_DIR/APKBUILD" cyolo-apk-builder:/tmp/guacd-build/
docker cp "$SCRIPT_DIR/entrypoint.sh" cyolo-apk-builder:/tmp/guacd-build/

rm /tmp/guacamole.tar.gz /tmp/bundled-libs.tar.gz

docker exec -u root cyolo-apk-builder chown -R builder:builder /tmp/guacd-build
echo ""

# Build the APK
echo "🔨 Building APK package..."
docker exec -u builder cyolo-apk-builder sh -c "
  cd /tmp/guacd-build && \
  abuild checksum && \
  abuild -r -F
" || { echo "❌ APK build failed"; exit 1; }

echo "✅ APK built successfully"
echo ""

# Locate and copy APK to output
echo "📋 Copying APK to output directory..."
mkdir -p "$OUTPUT_DIR"

# Find the most recently modified APK (in case old builds exist)
APK_PATH=$(docker exec cyolo-apk-builder sh -c "find /home/builder/packages/tmp -name 'cyolo-guacd-*.apk' -type f -exec ls -t {} + 2>/dev/null | head -1")
if [ -z "$APK_PATH" ]; then
    echo "❌ APK file not found!"
    echo "Looking in /home/builder/packages:"
    docker exec cyolo-apk-builder find /home/builder/packages -name 'cyolo-guacd-*.apk' -type f
    exit 1
fi

APK_NAME=$(basename "$APK_PATH")

docker exec cyolo-apk-builder cp "$APK_PATH" /output/
cp "$APK_BUILDER_DIR/output/$APK_NAME" "$OUTPUT_DIR/" 2>/dev/null || true

echo "   APK: $APK_NAME"
echo "   Location: $OUTPUT_DIR/$APK_NAME"
echo ""

# Display APK info
echo "📊 APK Information:"
echo "   Size: $(docker exec cyolo-apk-builder ls -lh "$APK_PATH" | awk '{print $5}')"
echo ""
echo "   Bundled libraries: $BUNDLED_COUNT shared objects"
echo ""
echo "   Contents (first 25 files):"
docker exec cyolo-apk-builder tar -tzf "$APK_PATH" | head -25
echo "   ... (truncated)"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Build complete!"
echo ""
echo "APK package ready:"
echo "  $OUTPUT_DIR/$APK_NAME"
echo ""
echo "This is a self-contained package with all dependencies bundled."
echo "It can run on Alpine 3.22+ despite being built on Alpine 3.18.6."
echo ""
echo "Next steps:"
echo "  1. Test extraction:"
echo "     mkdir -p /tmp/test-guacd"
echo "     tar -xzf $OUTPUT_DIR/$APK_NAME -C /tmp/test-guacd"
echo "     ls -la /tmp/test-guacd/software/deps/guacd/$GUACD_VERSION/"
echo ""
echo "  2. Test in cyolauncher (will be handled by cyolauncher's installer)"
echo ""
