#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Pinned Agora Voice SDK for macOS (4.x, dynamic).
#
# HOW TO UPDATE THIS SCRIPT:
#   1. Visit https://docs.agora.io/en/sdks?platform=macos and locate the
#      macOS Native SDK release you want (only the FULL package is available
#      publicly and contains AgoraRtcKit.framework).
#   2. The public CDN pattern is:
#        https://download.agora.io/sdk/release/Agora_Native_SDK_for_Mac_v<VERSION>_FULL.zip
#      (No login required. Probe with: curl -fsIL "<url>" -o /dev/null -w "%{http_code}")
#   3. Update SDK_VERSION and SDK_URL below.
#   4. Run this script once with SDK_SHA256 still set to "PUT_SHA256_HERE" —
#      it will print the real sha256. Paste that value into SDK_SHA256.
#   5. Commit only scripts/fetch-sdk.sh (third_party/agora/ is gitignored).
#
# NOTE: The FULL SDK zip contains xcframework wrappers. Inside each
#       *.xcframework/macos-arm64_x86_64/ lives the actual *.framework.
#       This script extracts all .framework bundles found at that level.
# ---------------------------------------------------------------------------
SDK_VERSION="4.4.0"
SDK_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Mac_v${SDK_VERSION}_FULL.zip"
SDK_SHA256="540a46d3b301232275b4fa4ed38782680797789d042b4f8cda43bdcd2b7da027"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/agora"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading Agora Voice SDK $SDK_VERSION ..."
curl -fSL -o "$TMP/sdk.zip" "$SDK_URL"

if [ "$SDK_SHA256" != "PUT_SHA256_HERE" ]; then
  echo "$SDK_SHA256  $TMP/sdk.zip" | shasum -a 256 -c -
else
  echo "WARNING: checksum not pinned. Computed sha256:"
  shasum -a 256 "$TMP/sdk.zip"
fi

unzip -q "$TMP/sdk.zip" -d "$TMP/unzipped"

# AgoraRtcKit.framework lives inside AgoraRtcKit.xcframework/macos-arm64_x86_64/
FRAMEWORK="$(find "$TMP/unzipped" -path '*/macos-*/*' -name 'AgoraRtcKit.framework' -type d | head -n1)"
# Fallback: if no xcframework slice layout, match anywhere.
if [ -z "$FRAMEWORK" ]; then
  FRAMEWORK="$(find "$TMP/unzipped" -name 'AgoraRtcKit.framework' -type d | head -n1)"
fi
if [ -z "$FRAMEWORK" ]; then
  echo "ERROR: AgoraRtcKit.framework not found in the package" >&2
  exit 1
fi

STAGE="$TMP/stage"
mkdir -p "$STAGE"
cp -R "$FRAMEWORK" "$STAGE/"
for fw in "$(dirname "$FRAMEWORK")"/*.framework; do
  [ "$fw" = "$FRAMEWORK" ] && continue
  cp -R "$fw" "$STAGE/"
done
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$STAGE" "$DEST"
echo "Installed frameworks into $DEST:"
ls "$DEST"
