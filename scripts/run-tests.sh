#!/bin/bash
# Wrapper script to run unit tests on macOS
# Re-signs the test binary with entitlements before running

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_macos_dev"

# Accept test name as argument, default to test_encoder
TEST_NAME="${1:-test_encoder}"
TEST_BINARY="$BUILD_DIR/RelWithDebInfo/$TEST_NAME"
ENTITLEMENTS="$PROJECT_ROOT/test/test_entitlements.plist"

if [[ ! -f "$TEST_BINARY" ]]; then
    echo "Error: Test binary not found at $TEST_BINARY"
    echo "Run: cmake --build build_macos_dev --target $TEST_NAME --config RelWithDebInfo"
    exit 1
fi

# Re-sign with entitlements to allow loading Homebrew libraries
codesign --force --sign - --entitlements "$ENTITLEMENTS" "$TEST_BINARY"

# Run the test
"$TEST_BINARY"

