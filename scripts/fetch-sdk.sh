#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# OS-aware Agora Voice SDK downloader.
# Supports macOS (frameworks → third_party/agora/*.framework)
#         and Windows git-bash (headers/libs/DLLs → third_party/agora/{include,lib,bin}/)
#
# HOW TO UPDATE THIS SCRIPT:
#   1. Visit https://docs.agora.io/en/sdks and locate the SDK release you want.
#   2. Update SDK_VERSION below.
#   3. Run once with the SHA256 placeholder still in place — the script will
#      print the real hash. Paste it into the matching MAC_SHA256 / WIN_SHA256.
#   4. Commit only scripts/fetch-sdk.sh (third_party/agora/ is gitignored).
#
# macOS note: The FULL SDK zip contains xcframework wrappers. Inside each
#   *.xcframework/macos-arm64_x86_64/ lives the actual *.framework.
#   This script extracts all .framework bundles found at that level.
#
# Windows note: WIN_SHA256 is a placeholder until the first CI run prints it.
#   The first run will print the computed hash; pin it and re-commit.
# ---------------------------------------------------------------------------
SDK_VERSION="4.4.0"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

OS="$(uname -s)"
case "$OS" in
  Darwin)
    # ===== macOS — behaviorally unchanged =====
    MAC_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Mac_v${SDK_VERSION}_FULL.zip"
    MAC_SHA256="540a46d3b301232275b4fa4ed38782680797789d042b4f8cda43bdcd2b7da027"
    DEST="$ROOT/third_party/agora"

    echo "Downloading Agora Voice SDK $SDK_VERSION ..."
    curl -fSL -o "$TMP/sdk.zip" "$MAC_URL"

    if [ "$MAC_SHA256" != "PUT_SHA256_HERE" ]; then
      echo "$MAC_SHA256  $TMP/sdk.zip" | shasum -a 256 -c -
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

    # Copy all frameworks from every xcframework's macos slice
    while IFS= read -r fw; do
      cp -R "$fw" "$STAGE/"
    done < <(find "$TMP/unzipped" -path '*/macos-*/*.framework' -type d -maxdepth 6)

    rm -rf "$DEST"
    mkdir -p "$(dirname "$DEST")"
    mv "$STAGE" "$DEST"
    echo "Installed frameworks into $DEST:"
    ls "$DEST"

    # Ad-hoc sign all frameworks so the hardened-runtime binary can load them
    # (avoids "Team ID mismatch" / library-validation errors at runtime)
    echo "Ad-hoc signing frameworks in $DEST ..."
    codesign --force --sign - "$DEST"/*.framework
    echo "Done."
    ;;

  MINGW*|MSYS*|CYGWIN*)
    # ===== Windows (git-bash / MSYS2 / Cygwin on GitHub Actions runner) =====
    WIN_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Windows_v${SDK_VERSION}_FULL.zip"
    WIN_SHA256="PUT_WINDOWS_SHA256_HERE"   # first CI run prints it; pin then
    DEST="$ROOT/third_party/agora"

    echo "Downloading Windows SDK ${SDK_VERSION} ..."
    curl -fSL -o "$TMP/sdk.zip" "$WIN_URL"

    if [ "$WIN_SHA256" != "PUT_WINDOWS_SHA256_HERE" ]; then
      echo "${WIN_SHA256}  $TMP/sdk.zip" | sha256sum -c -
    else
      echo "WARNING: Windows SDK checksum not pinned. Computed sha256:"
      sha256sum "$TMP/sdk.zip"
    fi

    unzip -q "$TMP/sdk.zip" -d "$TMP/x"
    rm -rf "$DEST"; mkdir -p "$DEST/include" "$DEST/lib" "$DEST/bin"

    inc_hdr="$(find "$TMP/x" -name 'IAgoraRtcEngine.h' | head -n1)"
    if [ -z "$inc_hdr" ]; then
      echo "ERROR: IAgoraRtcEngine.h not found in Windows SDK" >&2
      exit 1
    fi
    cp -R "$(dirname "$inc_hdr")"/. "$DEST/include/"

    # x64 import libs + runtime DLLs (the SDK nests these under an x86_64/x64 dir)
    find "$TMP/x" \( -path '*x86_64*' -o -path '*x64*' \) -name '*.lib' -exec cp {} "$DEST/lib/" \;
    find "$TMP/x" \( -path '*x86_64*' -o -path '*x64*' \) -name '*.dll' -exec cp {} "$DEST/bin/" \;

    echo "Windows SDK normalized:"
    ls "$DEST/include" | head
    echo "--- lib ---"; ls "$DEST/lib"
    echo "--- bin ---"; ls "$DEST/bin"
    ;;

  *)
    echo "Unsupported OS: $OS" >&2
    exit 1
    ;;
esac
