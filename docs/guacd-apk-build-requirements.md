# guacd Alpine APK Build Requirements

**Status:** Requirements Document  
**Version:** 1.0  
**Last Updated:** December 2025  
**Target Version:** guacd 1.5.5

---

## Table of Contents

1. [Overview](#overview)
2. [Build Environment](#build-environment)
3. [Installation Layout](#installation-layout)
4. [Dependency Bundling](#dependency-bundling)
5. [Entrypoint & Configuration](#entrypoint--configuration)
6. [GitHub Workflow](#github-workflow)
7. [APK Metadata](#apk-metadata)
8. [Testing Strategy](#testing-strategy)

---

## Overview

### Goal

Build guacd as a **self-contained Alpine APK** that:
- Bundles all shared library dependencies
- Can be built on Alpine 3.18.6 and run on Alpine 3.22+
- Supports both AMD64 and ARM64 architectures
- Integrates with cyolauncher's managed process system

### Why Bundle Dependencies?

Unlike typical APKs that rely on system libraries, this APK must be **forward-compatible** across Alpine versions. By bundling all .so files, we ensure guacd works regardless of the host Alpine version.

---

## Build Environment

### Base Build Image

```dockerfile
FROM alpine:3.18.6 AS builder
```

**Why 3.18.6?**
- Matches existing guacamole-server Dockerfile
- Has `openssl1.1-compat-dev` package (removed in later versions)
- Proven stable build environment

### Target Runtime

- **Minimum**: Alpine 3.18.6
- **Tested**: Alpine 3.22
- **Expected**: Any Alpine 3.x version

### Multi-Architecture Support

Build for:
- `linux/amd64` (x86_64)
- `linux/arm64` (aarch64)

Use Docker Buildx with QEMU for cross-platform builds.

---

## Installation Layout

### Directory Structure

```
/host/cyolo/software/guacd-{version}-{hash}/
├── bin/
│   └── entrypoint.sh          # Wrapper script
├── sbin/
│   └── guacd                  # Main binary
└── lib/
    ├── libguac*.so*           # Guacamole libraries
    ├── libguac-client-*.so    # Protocol plugins (RDP, VNC, SSH)
    ├── freerdp2/              # FreeRDP plugins
    │   └── *guac*.so
    ├── libcairo.so*           # Bundled system libraries
    ├── libpango*.so*
    ├── libjpeg*.so*
    ├── libpng*.so*
    ├── libssl.so*
    ├── libcrypto.so*
    └── ... (all runtime deps)
```

### Path Conventions

**Binary Location:**
- Use `sbin/` for the daemon (`guacd`)
  - `sbin` is conventional for system daemons
  - `bin` is for user-executable utilities
- Use `bin/` for wrapper scripts (`entrypoint.sh`)

**Libraries:**
- All .so files in `lib/`
- Preserve subdirectory structure (e.g., `freerdp2/`)

---

## Dependency Bundling

### Build Dependencies (Alpine 3.18.6)

From Dockerfile:
```
autoconf, automake, build-base, cairo-dev, cmake, git, grep,
libjpeg-turbo-dev, libpng-dev, libtool, libwebp-dev, make,
openssl1.1-compat-dev, pango-dev, pulseaudio-dev, util-linux-dev
```

### Runtime Dependencies to Bundle

From Dockerfile lines 169-180:
```
ca-certificates, font-noto-cjk, ghostscript, netcat-openbsd,
shadow, terminus-font, ttf-dejavu, ttf-liberation, util-linux-login
+ all dependencies from ${PREFIX_DIR}/DEPENDENCIES
```

### Dependency Collection Strategy

1. **Build guacd with all dependencies** to `/opt/guacamole`

2. **Generate dependency list**:
   ```bash
   src/guacd-docker/bin/list-dependencies.sh \
       /opt/guacamole/sbin/guacd \
       /opt/guacamole/lib/libguac-client-*.so \
       /opt/guacamole/lib/freerdp2/*guac*.so \
       > /opt/guacamole/DEPENDENCIES
   ```

3. **Install runtime APK packages** and copy their .so files:
   ```bash
   # Install all runtime packages
   apk add --no-cache ca-certificates font-noto-cjk ghostscript \
       netcat-openbsd shadow terminus-font ttf-dejavu \
       ttf-liberation util-linux-login
   
   # Install packages from DEPENDENCIES file
   xargs apk add --no-cache < /opt/guacamole/DEPENDENCIES
   
   # Find and copy all .so files from installed packages
   ldd /opt/guacamole/sbin/guacd | grep '=>' | awk '{print $3}' | \
       xargs -I {} cp {} /opt/guacamole/lib/
   
   # Recursively resolve dependencies of copied libraries
   for lib in /opt/guacamole/lib/*.so*; do
       ldd $lib | grep '=>' | awk '{print $3}' | \
           xargs -I {} cp {} /opt/guacamole/lib/
   done
   ```

4. **Include fonts and certificates**:
   - Fonts to `share/fonts/` (for RDP/VNC rendering)
   - CA certificates to `share/ca-certificates/`

### Library Path Resolution

At runtime, set:
```bash
export LD_LIBRARY_PATH=/host/cyolo/software/guacd-{version}-{hash}/lib
```

---

## Entrypoint & Configuration

### entrypoint.sh

Location: `bin/entrypoint.sh`

```bash
#!/bin/sh
set -e

# Resolve installation directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$(dirname "$SCRIPT_DIR")"

# Set library path for bundled dependencies
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib"
export LC_ALL=C.UTF-8

# Default configuration
GUACD_LOG_LEVEL="${GUACD_LOG_LEVEL:-info}"
GUAC_LISTEN_PORT="${GUAC_LISTEN_PORT:-4822}"

# Signal handling - forward signals to child process
trap 'kill -TERM "$child_pid" 2>/dev/null' TERM INT

# Start guacd in background
"${INSTALL_DIR}/sbin/guacd" \
    -b 0.0.0.0 \
    -l "${GUAC_LISTEN_PORT}" \
    -L "${GUACD_LOG_LEVEL}" \
    -f &

child_pid=$!

# Wait for guacd to exit
wait "$child_pid"
exit_code=$?

# Clean shutdown
sleep 1
exit $exit_code
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `GUACD_LOG_LEVEL` | `info` | Log level: `trace`, `debug`, `info`, `warning`, `error` |
| `GUAC_LISTEN_PORT` | `4822` | Port to listen on |
| `LD_LIBRARY_PATH` | `{install}/lib` | Path to bundled libraries (auto-set) |
| `LC_ALL` | `C.UTF-8` | Locale (auto-set) |

### Command Line Arguments

The entrypoint passes these to guacd:
- `-b 0.0.0.0` - Bind to all interfaces
- `-l ${GUAC_LISTEN_PORT}` - Listen port
- `-L ${GUACD_LOG_LEVEL}` - Log level
- `-f` - Foreground mode (no daemonization)

---

## GitHub Workflow

### File: `.github/workflows/build-guacd-apk.yml`

```yaml
name: Build guacd Alpine APK

on:
  workflow_dispatch:
    inputs:
      version:
        description: 'guacd version (e.g., 1.5.5)'
        required: true
      release_number:
        description: 'APK release number (e.g., 1)'
        required: true
        default: '1'

jobs:
  build_apk:
    name: Build guacd APK for ${{ matrix.arch }}
    runs-on: ubuntu-latest
    strategy:
      matrix:
        arch: [amd64, arm64]
    
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
      
      - name: Set up QEMU
        uses: docker/setup-qemu-action@v3
        with:
          platforms: ${{ matrix.arch }}
      
      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3
      
      - name: Build APK
        run: |
          # Build using Dockerfile.apk (to be created)
          docker buildx build \
            --platform linux/${{ matrix.arch }} \
            --build-arg VERSION=${{ inputs.version }} \
            --build-arg RELEASE=${{ inputs.release_number }} \
            --output type=local,dest=./output \
            -f Dockerfile.apk \
            .
      
      - name: Upload APK artifact
        uses: actions/upload-artifact@v4
        with:
          name: guacd-${{ inputs.version }}-r${{ inputs.release_number }}-${{ matrix.arch }}.apk
          path: output/*.apk
```

### Build Trigger

Manual workflow dispatch:
```bash
gh workflow run build-guacd-apk.yml \
  -f version=1.5.5 \
  -f release_number=14
```

Expected output:
- `cyolo-guacd-1.5.5-r14-aarch64.apk`
- `cyolo-guacd-1.5.5-r14-x86_64.apk`

---

## APK Metadata

### APKBUILD Structure

While we're building the APK via Docker (not `abuild`), we need to embed metadata:

```bash
# Package information
pkgname=cyolo-guacd
pkgver=1.5.5
pkgrel=14
pkgdesc="Apache Guacamole proxy daemon (Cyolo build)"
url="https://guacamole.apache.org/"
arch="aarch64 x86_64"
license="Apache-2.0"
depends=""  # Empty - we bundle everything

# Installation paths
prefix=/host/cyolo/software
install_dir=$prefix/guacd-$pkgver-$pkgrel

# Metadata for .PKGINFO
builddate=$(date -u +%s)
packager="Cyolo Build System"
```

### APK Package Naming

Format: `cyolo-guacd-{VERSION}-r{RELEASE}-{ARCH}.apk`

Examples:
- `cyolo-guacd-1.5.5-r14-aarch64.apk`
- `cyolo-guacd-1.5.5-r14-x86_64.apk`

### Version Hash for Directory Name

The `{hash}` in `/host/cyolo/software/guacd-{version}-{hash}/` should be:
- First 16 characters of SHA256 of the APK file
- Example: `guacd-1.5.5-34d31927086e8d51`

This ensures unique directory names for different builds of the same version.

---

## Testing Strategy

### Build Verification

1. **APK Structure Check**:
   ```bash
   tar -tzf cyolo-guacd-1.5.5-r14-aarch64.apk
   # Should contain:
   # - sbin/guacd
   # - bin/entrypoint.sh
   # - lib/*.so*
   ```

2. **Binary Verification**:
   ```bash
   # Extract and check binary
   tar -xzf cyolo-guacd-1.5.5-r14-aarch64.apk
   file sbin/guacd
   # Expected: ELF 64-bit LSB executable, ARM aarch64
   
   ldd sbin/guacd
   # All dependencies should resolve to ./lib/
   ```

### Runtime Testing

1. **Alpine 3.18.6 (Build Platform)**:
   ```bash
   docker run --rm -it \
     -v $(pwd):/host/cyolo/software/guacd-1.5.5-test \
     alpine:3.18.6 \
     /host/cyolo/software/guacd-1.5.5-test/bin/entrypoint.sh
   ```

2. **Alpine 3.22 (Target Platform)**:
   ```bash
   docker run --rm -it \
     -v $(pwd):/host/cyolo/software/guacd-1.5.5-test \
     alpine:3.22 \
     /host/cyolo/software/guacd-1.5.5-test/bin/entrypoint.sh
   ```

3. **Connectivity Test**:
   ```bash
   # In separate terminal
   nc localhost 4822
   # Should connect (guacd ready)
   ```

4. **Log Output Verification**:
   ```
   Expected startup log:
   guacd[1234]: Guacamole proxy daemon (guacd) version 1.5.5 started
   guacd[1234]: Listening on host 0.0.0.0, port 4822
   ```

### Integration Test with cyolauncher

```yaml
# In idac-schema.yaml managed_processes section
managed_processes:
  guacd:
    package: "guacd"
    mode: "manual"
    runtime:
      command: ["/bin/sh", "./bin/entrypoint.sh"]
      working_dir: "{software}/deps/guacd/{version}"
```

Test:
1. Extract APK to `/host/cyolo/software/deps/guacd/1.5.5-34d31927/`
2. Run cyolauncher with managed process config
3. IDAC starts guacd via Unix socket API
4. Verify guacd listens on expected port
5. Connect via RDP/VNC client through IDAC

---

## Implementation Checklist

### Phase 1: Docker Build Script

- [ ] Create `Dockerfile.apk` for multi-stage APK build
- [ ] Implement dependency collection script
- [ ] Bundle all .so files into lib/
- [ ] Create entrypoint.sh wrapper
- [ ] Generate APK metadata (.PKGINFO)
- [ ] Package as .apk tarball

### Phase 2: GitHub Workflow

- [ ] Create `.github/workflows/build-guacd-apk.yml`
- [ ] Configure QEMU for multi-arch builds
- [ ] Set up artifact upload
- [ ] Test workflow with manual dispatch

### Phase 3: Testing

- [ ] Verify APK structure
- [ ] Test on Alpine 3.18.6
- [ ] Test on Alpine 3.22
- [ ] Verify all .so dependencies resolve
- [ ] Test entrypoint.sh environment handling

### Phase 4: Integration

- [ ] Upload APK to package repository
- [ ] Update cyolauncher to download guacd APK
- [ ] Test managed process lifecycle
- [ ] Verify IDAC can connect to guacd

---

## Open Questions

1. **APK Repository**: Where will the APKs be hosted?
   - Option A: GitHub Releases
   - Option B: Cyolo package service (packages.cyolo.co)
   - Option C: S3/CDN

2. **Signing**: Should APKs be signed?
   - If yes, need to set up APK signing keys
   - If no, need `--allow-untrusted` flag

3. **Font Handling**: Do fonts need to be bundled?
   - RDP/VNC rendering may need fonts
   - Fonts are large (~50MB)
   - Consider: bundle minimal set or rely on host

4. **CA Certificates**: Bundle or use host?
   - guacd doesn't typically make HTTPS requests
   - If needed, bundle `/etc/ssl/certs/ca-certificates.crt`

---

## References

- [guacamole-server Dockerfile](https://github.com/cyolosecurity/guacamole-server/blob/v1.5.5/Dockerfile)
- [Alpine APK format specification](https://wiki.alpinelinux.org/wiki/Apk_spec)
- [Docker multi-platform builds](https://docs.docker.com/build/building/multi-platform/)

---

**Document Status:** Ready for Implementation  
**Next Steps:** Create Dockerfile.apk and build-guacd-apk.yml workflow


