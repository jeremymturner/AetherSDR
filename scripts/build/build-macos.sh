#!/usr/bin/env bash
# build-macos.sh — configure, build, deploy, and sign AetherSDR.app on macOS
#
# Usage: ./scripts/build/build-macos.sh [OPTIONS]
#
#   -b <dir>   Build directory (default: build-mac)
#   -j <n>     Parallel jobs (default: logical CPU count)
#   -t <type>  CMake build type: Debug|RelWithDebInfo|Release (default: RelWithDebInfo)
#   --clean    Wipe the build directory before configuring
#   --dmg      Create a .dmg after building (requires create-dmg or hdiutil)
#   --no-sign  Skip ad-hoc code signing (app will not run on macOS 15+)
#
# Requires: cmake, ninja, brew (for Qt path detection)

set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────
BUILD_DIR="build-mac"
BUILD_TYPE="RelWithDebInfo"
JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CLEAN=0
MAKE_DMG=0
SKIP_SIGN=0

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b) BUILD_DIR="$2"; shift 2 ;;
        -j) JOBS="$2";      shift 2 ;;
        -t) BUILD_TYPE="$2"; shift 2 ;;
        --clean)   CLEAN=1;    shift ;;
        --dmg)     MAKE_DMG=1; shift ;;
        --no-sign) SKIP_SIGN=1; shift ;;
        -h|--help)
            sed -n '/^# Usage:/,/^[^#]/p' "$0" | grep '^#' | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── Locate repo root ──────────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

VERSION=$(grep -m1 'project(AetherSDR' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "0.0.0")
APP_BUNDLE="${BUILD_DIR}/AetherSDR.app"

echo "=== AetherSDR macOS build v${VERSION} ==="
echo "    Build dir : ${BUILD_DIR}"
echo "    Build type: ${BUILD_TYPE}"
echo "    Jobs      : ${JOBS}"

# ── Locate Qt ────────────────────────────────────────────────────────────────
# Homebrew splits Qt across qtbase (has Qt6Config.cmake) and separate module
# formulae. Qt6Config.cmake searches for additional modules via
# QT_ADDITIONAL_PACKAGES_PREFIX_PATH; setting it to /opt/homebrew lets it find
# Qt6Multimedia, Qt6WebSockets, etc. from their own Cellar entries.
find_qt6_dir() {
    # Prefer a user-supplied Qt6_DIR
    if [[ -n "${Qt6_DIR:-}" ]]; then
        echo "$Qt6_DIR"
        return
    fi
    # Homebrew qtbase (the formula that owns Qt6Config.cmake)
    local brew_prefix
    brew_prefix="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
    local candidates=(
        "${brew_prefix}/Cellar/qtbase/$(ls "${brew_prefix}/Cellar/qtbase" 2>/dev/null | sort -V | tail -1)/lib/cmake/Qt6"
        "${brew_prefix}/lib/cmake/Qt6"
        "/usr/local/lib/cmake/Qt6"
    )
    for d in "${candidates[@]}"; do
        if [[ -f "${d}/Qt6Config.cmake" ]]; then
            echo "$d"
            return
        fi
    done
    echo ""
}

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
QT6_DIR="$(find_qt6_dir)"

if [[ -z "$QT6_DIR" ]]; then
    echo "ERROR: Qt6Config.cmake not found." >&2
    echo "Install Qt with: brew install qt" >&2
    exit 1
fi
echo "    Qt6 dir   : ${QT6_DIR}"

# ── Optional: locate qtkeychain ──────────────────────────────────────────────
KEYCHAIN_PREFIX=""
if [[ -d "${BREW_PREFIX}/opt/qtkeychain" ]]; then
    KEYCHAIN_PREFIX="${BREW_PREFIX}/opt/qtkeychain"
fi

# ── Locate platform plugin ────────────────────────────────────────────────────
# macdeployqt from qtbase sometimes omits libqcocoa.dylib; we copy it manually.
COCOA_PLUGIN=""
for candidate in \
    "${BREW_PREFIX}/Cellar/qtbase/$(ls "${BREW_PREFIX}/Cellar/qtbase" 2>/dev/null | sort -V | tail -1)/share/qt/plugins/platforms/libqcocoa.dylib" \
    "${BREW_PREFIX}/share/qt/plugins/platforms/libqcocoa.dylib" \
    "${BREW_PREFIX}/opt/qt/share/qt/plugins/platforms/libqcocoa.dylib"
do
    if [[ -f "$candidate" ]]; then
        COCOA_PLUGIN="$candidate"
        break
    fi
done

if [[ -z "$COCOA_PLUGIN" ]]; then
    echo "WARNING: libqcocoa.dylib not found — app may crash at startup." >&2
fi

# ── Locate macdeployqt ────────────────────────────────────────────────────────
MACDEPLOYQT=""
for candidate in \
    "${BREW_PREFIX}/Cellar/qtbase/$(ls "${BREW_PREFIX}/Cellar/qtbase" 2>/dev/null | sort -V | tail -1)/bin/macdeployqt" \
    "${BREW_PREFIX}/bin/macdeployqt" \
    "${BREW_PREFIX}/opt/qt/bin/macdeployqt"
do
    if [[ -x "$candidate" ]]; then
        MACDEPLOYQT="$candidate"
        break
    fi
done

if [[ -z "$MACDEPLOYQT" ]]; then
    echo "ERROR: macdeployqt not found. Install Qt with: brew install qt" >&2
    exit 1
fi
echo "    macdeployqt: ${MACDEPLOYQT}"

# ── Clean ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
    echo "--- Cleaning ${BUILD_DIR} ---"
    /bin/rm -rf "${BUILD_DIR}"
fi

# ── Configure ─────────────────────────────────────────────────────────────────
echo ""
echo "--- Configuring ---"
CMAKE_ARGS=(
    -B "${BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DQt6_DIR="${QT6_DIR}"
    -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="${BREW_PREFIX}"
)
[[ -n "$KEYCHAIN_PREFIX" ]] && CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${KEYCHAIN_PREFIX}")

cmake "${CMAKE_ARGS[@]}"

# ── Build ─────────────────────────────────────────────────────────────────────
echo ""
echo "--- Building (${JOBS} jobs) ---"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

# ── Deploy Qt frameworks ──────────────────────────────────────────────────────
echo ""
echo "--- Running macdeployqt ---"
"${MACDEPLOYQT}" "${APP_BUNDLE}" -verbose=1

# ── Platform plugin ───────────────────────────────────────────────────────────
# macdeployqt from qtbase omits libqcocoa.dylib because the plugins directory
# lives in a sibling Cellar formula (qtbase ships the cmake configs; the qt
# formula ships the runtime plugins). Copy it manually.
PLATFORMS_DIR="${APP_BUNDLE}/Contents/PlugIns/platforms"
if [[ -n "$COCOA_PLUGIN" ]] && [[ ! -f "${PLATFORMS_DIR}/libqcocoa.dylib" ]]; then
    echo "--- Copying libqcocoa.dylib (platform plugin) ---"
    mkdir -p "${PLATFORMS_DIR}"
    cp "${COCOA_PLUGIN}" "${PLATFORMS_DIR}/"
fi

# ── Code signing ──────────────────────────────────────────────────────────────
# macOS 15+ (Sequoia) and macOS 26 enforce per-page signature validation for
# all dylibs loaded by an app. macdeployqt rewrites install names and then
# re-signs with ad-hoc '-', but leaves some dylibs (e.g. libbrotlicommon)
# with broken signatures. Re-sign every binary in the bundle, innermost first.
if [[ $SKIP_SIGN -eq 0 ]]; then
    echo "--- Re-signing bundle (ad-hoc) ---"

    # 1. All loose dylibs (deepest first so dependents are signed before signers)
    find "${APP_BUNDLE}" -type f -name "*.dylib" | while read -r f; do
        codesign --force --sign - "$f" 2>&1 | grep -v "^$" || true
    done

    # 2. Framework bundles
    find "${APP_BUNDLE}" -type d -name "*.framework" | while read -r fw; do
        codesign --force --sign - "$fw" 2>&1 | grep -v "^$" || true
    done

    # 3. Main executable, then the app bundle itself
    codesign --force --sign - "${APP_BUNDLE}/Contents/MacOS/AetherSDR"
    codesign --force --sign - "${APP_BUNDLE}"

    # 4. Verify
    codesign --verify --deep --strict "${APP_BUNDLE}" \
        && echo "    Signature: OK" \
        || echo "    WARNING: signature verification failed"
fi

# ── Optional DMG ─────────────────────────────────────────────────────────────
if [[ $MAKE_DMG -eq 1 ]]; then
    DMG_NAME="AetherSDR-${VERSION}-macOS-apple-silicon.dmg"
    DMG_PATH="${BUILD_DIR}/${DMG_NAME}"
    echo ""
    echo "--- Creating DMG: ${DMG_NAME} ---"

    if command -v create-dmg &>/dev/null; then
        create-dmg \
            --volname "AetherSDR ${VERSION}" \
            --window-size 560 400 \
            --icon-size 128 \
            --icon "AetherSDR.app" 140 185 \
            --app-drop-link 420 185 \
            "${DMG_PATH}" \
            "${APP_BUNDLE}"
    else
        # Fallback: plain hdiutil DMG (no fancy layout)
        TMP_DMG_DIR="$(mktemp -d)"
        cp -R "${APP_BUNDLE}" "${TMP_DMG_DIR}/"
        ln -s /Applications "${TMP_DMG_DIR}/Applications"
        hdiutil create \
            -volname "AetherSDR ${VERSION}" \
            -srcfolder "${TMP_DMG_DIR}" \
            -ov -format UDZO \
            "${DMG_PATH}"
        rm -rf "${TMP_DMG_DIR}"
    fi

    echo "    DMG: ${DMG_PATH}"
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "=== Build complete ==="
echo "    App bundle : ${REPO_ROOT}/${APP_BUNDLE}"
echo "    Run with   : open ${APP_BUNDLE}"
