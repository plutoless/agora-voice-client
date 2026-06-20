#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/agora-voice-client-macos-arm64"

test -f "$ROOT/build/agora-voice-client" || { echo "build first: cmake --build build" >&2; exit 1; }
test -d "$ROOT/third_party/agora/AgoraRtcKit.framework" || { echo "run scripts/fetch-sdk.sh first" >&2; exit 1; }

rm -rf "$OUT" && mkdir -p "$OUT"
cp "$ROOT/build/agora-voice-client" "$OUT/"
# Copy all Agora frameworks next to the binary.
cp -R "$ROOT"/third_party/agora/*.framework "$OUT/"

# Make the binary find frameworks beside itself, then re-sign (install_name_tool breaks the signature).
install_name_tool -add_rpath @loader_path "$OUT/agora-voice-client"
codesign --force --options runtime \
  --entitlements "$ROOT/entitlements.plist" \
  --sign - "$OUT/agora-voice-client"

( cd "$ROOT/dist" && tar -czf agora-voice-client-macos-arm64.tar.gz agora-voice-client-macos-arm64 )
echo "Wrote $ROOT/dist/agora-voice-client-macos-arm64.tar.gz"
