#!/usr/bin/env zsh

# build-universal-freetype.sh
#
# Build FreeType as a universal binary (arm64 + x86_64) for macOS.
# This script is intended for CI environments or local development where
# a universal FreeType is needed for universal plugin builds.
#
# Usage:
#   ./scripts/build-universal-freetype.sh [version]
#
# The universal FreeType will be installed to:
#   .deps/freetype-universal/

builtin emulate -L zsh
setopt EXTENDED_GLOB
setopt PUSHD_SILENT
setopt ERR_EXIT
setopt ERR_RETURN
setopt NO_UNSET
setopt PIPE_FAIL

FREETYPE_VERSION="${1:-2.13.2}"
SCRIPT_DIR="${0:A:h}"
PROJECT_ROOT="${SCRIPT_DIR:h}"
DEPS_DIR="${PROJECT_ROOT}/.deps"
FREETYPE_UNIVERSAL_DIR="${DEPS_DIR}/freetype-universal"
BUILD_DIR="${DEPS_DIR}/freetype-build"

print "Building FreeType ${FREETYPE_VERSION} as universal binary..."
print "Output directory: ${FREETYPE_UNIVERSAL_DIR}"

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${FREETYPE_UNIVERSAL_DIR}"

# Download FreeType source
FREETYPE_URL="https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz"
FREETYPE_ARCHIVE="${BUILD_DIR}/freetype-${FREETYPE_VERSION}.tar.xz"
FREETYPE_SRC="${BUILD_DIR}/freetype-${FREETYPE_VERSION}"

if [[ ! -f "${FREETYPE_ARCHIVE}" ]]; then
  print "Downloading FreeType ${FREETYPE_VERSION}..."
  curl -L -o "${FREETYPE_ARCHIVE}" "${FREETYPE_URL}"
fi

# Extract source
if [[ ! -d "${FREETYPE_SRC}" ]]; then
  print "Extracting FreeType source..."
  tar -xf "${FREETYPE_ARCHIVE}" -C "${BUILD_DIR}"
fi

# Build for arm64
print "Building for arm64..."
BUILD_ARM64="${BUILD_DIR}/build-arm64"
mkdir -p "${BUILD_ARM64}"
pushd "${BUILD_ARM64}"

CFLAGS="-arch arm64 -mmacosx-version-min=12.0" \
LDFLAGS="-arch arm64" \
"${FREETYPE_SRC}/configure" \
  --prefix="${BUILD_ARM64}/install" \
  --enable-static \
  --disable-shared \
  --without-harfbuzz \
  --without-brotli \
  --without-bzip2 \
  --without-png \
  --with-zlib=yes

make -j$(sysctl -n hw.ncpu)
make install

popd

# Build for x86_64
print "Building for x86_64..."
BUILD_X86_64="${BUILD_DIR}/build-x86_64"
mkdir -p "${BUILD_X86_64}"
pushd "${BUILD_X86_64}"

CFLAGS="-arch x86_64 -mmacosx-version-min=12.0" \
LDFLAGS="-arch x86_64" \
"${FREETYPE_SRC}/configure" \
  --prefix="${BUILD_X86_64}/install" \
  --enable-static \
  --disable-shared \
  --without-harfbuzz \
  --without-brotli \
  --without-bzip2 \
  --without-png \
  --with-zlib=yes

make -j$(sysctl -n hw.ncpu)
make install

popd

# Create universal binary
print "Creating universal binary..."
mkdir -p "${FREETYPE_UNIVERSAL_DIR}/lib"
mkdir -p "${FREETYPE_UNIVERSAL_DIR}/include"

lipo -create \
  "${BUILD_ARM64}/install/lib/libfreetype.a" \
  "${BUILD_X86_64}/install/lib/libfreetype.a" \
  -output "${FREETYPE_UNIVERSAL_DIR}/lib/libfreetype.a"

# Copy headers (they should be identical between architectures)
cp -R "${BUILD_ARM64}/install/include/"* "${FREETYPE_UNIVERSAL_DIR}/include/"

# Copy pkgconfig (use arm64 version and update prefix)
mkdir -p "${FREETYPE_UNIVERSAL_DIR}/lib/pkgconfig"
sed "s|${BUILD_ARM64}/install|${FREETYPE_UNIVERSAL_DIR}|g" \
  "${BUILD_ARM64}/install/lib/pkgconfig/freetype2.pc" \
  > "${FREETYPE_UNIVERSAL_DIR}/lib/pkgconfig/freetype2.pc"

# Remove quarantine attributes from the universal directory
print "Removing quarantine attributes..."
xattr -r -d com.apple.quarantine "${FREETYPE_UNIVERSAL_DIR}" 2>/dev/null || true

# Set proper permissions
chmod -R u+w "${FREETYPE_UNIVERSAL_DIR}"

print "✓ Universal FreeType built successfully"
print "  Location: ${FREETYPE_UNIVERSAL_DIR}"
print "  Version: ${FREETYPE_VERSION}"

# Verify universal binary
print "\nVerifying universal binary..."
lipo -info "${FREETYPE_UNIVERSAL_DIR}/lib/libfreetype.a"

# Clean up build directories to save space
print "\nCleaning up build artifacts..."
rm -rf "${BUILD_ARM64}" "${BUILD_X86_64}" "${FREETYPE_SRC}"

print "✓ Build artifacts cleaned up"

