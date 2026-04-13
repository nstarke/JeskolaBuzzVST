#!/usr/bin/env bash
# Build BuzzBridge for use under WINE + yabridge on Linux.
#
# Produces a 32-bit BuzzBridge.vst3 bundle that yabridge can expose to Linux
# DAWs. Only the 32-bit plugin is built — the 64-bit bridge (BuzzBridgeHost32)
# is unnecessary because yabridge already handles the 64<->32 boundary.
#
# Usage:
#   ./scripts/build-linux-wine.sh              # build only
#   ./scripts/build-linux-wine.sh --install    # build + yabridgectl add
#
# Requirements on host:
#   - bash, curl, tar, cmake (>=3.25), git, wine (for yabridge runtime)
#   - yabridge + yabridgectl (only for --install)
#
# llvm-mingw is downloaded on first run into third_party/llvm-mingw unless
# LLVM_MINGW_ROOT is already set.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-wine"
DIST_DIR="${REPO_ROOT}/dist-wine"
TOOLCHAIN_FILE="${SCRIPT_DIR}/toolchain-llvm-mingw-i686.cmake"

LLVM_MINGW_VERSION="20250709"
LLVM_MINGW_RELEASE="llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64"
LLVM_MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${LLVM_MINGW_RELEASE}.tar.xz"

INSTALL_TO_YABRIDGE=0
for arg in "$@"; do
    case "$arg" in
        --install) INSTALL_TO_YABRIDGE=1 ;;
        -h|--help)
            sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 1 ;;
    esac
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing dependency: $1" >&2; exit 1; }; }
need cmake
need curl
need tar
need git

# --- fetch llvm-mingw if needed -------------------------------------------
if [[ -z "${LLVM_MINGW_ROOT:-}" ]]; then
    LLVM_MINGW_ROOT="${REPO_ROOT}/third_party/llvm-mingw/${LLVM_MINGW_RELEASE}"
    if [[ ! -x "${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang" ]]; then
        echo "[build-wine] downloading llvm-mingw ${LLVM_MINGW_VERSION}..."
        mkdir -p "${REPO_ROOT}/third_party/llvm-mingw"
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        curl -fL "${LLVM_MINGW_URL}" -o "${tmp}/llvm-mingw.tar.xz"
        tar -xJf "${tmp}/llvm-mingw.tar.xz" -C "${REPO_ROOT}/third_party/llvm-mingw"
    fi
fi
export LLVM_MINGW_ROOT
echo "[build-wine] LLVM_MINGW_ROOT=${LLVM_MINGW_ROOT}"

# --- submodules -----------------------------------------------------------
if [[ ! -f "${REPO_ROOT}/sdk/CMakeLists.txt" ]]; then
    echo "[build-wine] initializing VST3 SDK submodule..."
    git -C "${REPO_ROOT}" submodule update --init --recursive
fi

# --- patch VST3 SDK for mingw --------------------------------------------
# The SDK's alignedalloc.h assumes Windows == _MSC_VER and falls through to
# std::aligned_alloc otherwise, which ucrt mingw's libc doesn't provide.
# Extend the _MSC_VER branches to cover __MINGW32__ so we use mingw's
# _aligned_malloc / _aligned_free instead. Idempotent: guarded on an already
# patched marker.
SDK_ALIGN="${REPO_ROOT}/sdk/public.sdk/source/vst/utility/alignedalloc.h"
if [[ -f "${SDK_ALIGN}" ]] && ! grep -q '__MINGW32__' "${SDK_ALIGN}"; then
    echo "[build-wine] patching ${SDK_ALIGN#${REPO_ROOT}/} for mingw..."
    sed -i \
        -e 's|^#ifdef _MSC_VER$|#if defined(_MSC_VER) \|\| defined(__MINGW32__)|' \
        -e 's|^#elif defined(_MSC_VER)$|#elif defined(_MSC_VER) \|\| defined(__MINGW32__)|' \
        -e 's|^#if defined(_MSC_VER)$|#if defined(_MSC_VER) \|\| defined(__MINGW32__)|' \
        "${SDK_ALIGN}"
fi

# --- configure ------------------------------------------------------------
rm -rf "${BUILD_DIR}"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
    -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF \
    -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
    -DSMTG_RUN_VST_VALIDATOR=OFF \
    -DSMTG_CREATE_PLUGIN_LINK=OFF \
    -DSMTG_CREATE_BUNDLE_FOR_WINDOWS=OFF \
    -DSMTG_CREATE_MODULE_INFO=OFF

# --- build (plugin only; skip tests + BuzzBridgeHost32) -------------------
cmake --build "${BUILD_DIR}" --target BuzzBridge

# --- stage the yabridge-friendly plugin -----------------------------------
# With SMTG_CREATE_BUNDLE_FOR_WINDOWS=OFF the SDK produces a flat .vst3 DLL
# rather than a Contents/x86-win/ bundle directory. yabridge accepts both
# layouts, and flat is simpler since we don't need attrib/moduleinfotool.
BUILT_PLUGIN="$(find "${BUILD_DIR}" -type f -name 'BuzzBridge.vst3' | head -n1)"
if [[ -z "${BUILT_PLUGIN}" ]]; then
    echo "[build-wine] could not locate built BuzzBridge.vst3" >&2
    exit 1
fi

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"
cp -a "${BUILT_PLUGIN}" "${DIST_DIR}/BuzzBridge.vst3"
echo "[build-wine] plugin ready at: ${DIST_DIR}/BuzzBridge.vst3"

# --- optional: register with yabridge -------------------------------------
if [[ "${INSTALL_TO_YABRIDGE}" -eq 1 ]]; then
    need yabridgectl
    # yabridge scans directories, not individual bundles. Point it at dist-wine.
    yabridgectl add "${DIST_DIR}" || true
    yabridgectl sync
    echo "[build-wine] registered with yabridge. Rescan plugins in your DAW."
fi
