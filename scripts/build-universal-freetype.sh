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
FREETYPE_ARCHIVE="${BUILD_DIR}/freetype-${FREETYPE_VERSION}.tar.xz"
FREETYPE_SRC="${BUILD_DIR}/freetype-${FREETYPE_VERSION}"

# Known-good SHA-256 checksums for the release tarballs, keyed by version. The
# download is validated against this so a corrupt or truncated mirror response
# (e.g. an HTML "502 Bad Gateway" page saved as .tar.xz) is rejected instead of
# being handed to tar.
typeset -A FREETYPE_SHA256
FREETYPE_SHA256=(
  2.13.2 12991c4e55c506dd7f9b765933e62fd2be2e06d421505d7950a132e4f1bb484d
)

# Release tarball mirrors, tried in order. Savannah is upstream's home but is
# regularly flaky (intermittent 502s from its mirror redirects); SourceForge
# carries the identical release files as a reliable fallback.
FREETYPE_MIRRORS=(
  "https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VERSION}.tar.xz"
  "https://downloads.sourceforge.net/project/freetype/freetype2/${FREETYPE_VERSION}/freetype-${FREETYPE_VERSION}.tar.xz"
)

# Validate an archive: checksum (when a known-good one exists for this version)
# plus a structural test that xz/tar can actually read it.
verify_archive() {
  local archive="$1"

  [[ -f "${archive}" ]] || return 1

  local expected="${FREETYPE_SHA256[${FREETYPE_VERSION}]:-}"
  if [[ -n "${expected}" ]]; then
    local actual
    actual="$(shasum -a 256 "${archive}" | awk '{ print $1 }')"
    if [[ "${actual}" != "${expected}" ]]; then
      print "  checksum mismatch (expected ${expected}, got ${actual})"
      return 1
    fi
  fi

  tar -tf "${archive}" > /dev/null 2>&1 || return 1
  return 0
}

# (Re)download whenever there is no cached archive or the cached one fails
# validation, so a previously-saved error page can never be reused.
if [[ ! -f "${FREETYPE_ARCHIVE}" ]] || ! verify_archive "${FREETYPE_ARCHIVE}"; then
  rm -f "${FREETYPE_ARCHIVE}"
  print "Downloading FreeType ${FREETYPE_VERSION}..."

  downloaded=false
  for url in "${FREETYPE_MIRRORS[@]}"; do
    print "  trying ${url}"
    # --fail turns HTTP 4xx/5xx into a non-zero exit (no error page written);
    # --retry/--retry-all-errors ride out transient mirror failures; the
    # timeouts stop a dead mirror from hanging the job.
    if curl --fail --location --retry 3 --retry-all-errors \
         --connect-timeout 30 --max-time 600 \
         -o "${FREETYPE_ARCHIVE}" "${url}" && verify_archive "${FREETYPE_ARCHIVE}"; then
      downloaded=true
      break
    fi
    print "  mirror failed, trying next..."
    rm -f "${FREETYPE_ARCHIVE}"
  done

  if [[ "${downloaded}" != true ]]; then
    print -u2 "ERROR: could not download a valid FreeType ${FREETYPE_VERSION} archive from any mirror"
    exit 1
  fi
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

