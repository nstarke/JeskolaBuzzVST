#!/usr/bin/env bash
# Assemble a BuzzBridge-Linux-<version>.tar.gz release tarball from the
# output of build-linux-wine.sh. Designed to be called both from the CI
# release workflow and from a developer shell.
#
# Input:  dist-wine/BuzzBridge.vst3   (produced by build-linux-wine.sh)
# Output: dist-wine/BuzzBridge-Linux-<version>.tar.gz
#
# The version string is taken from the first positional arg, else
# `git describe --tags`, else "dev".

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist-wine"
PLUGIN_FILE="${DIST_DIR}/BuzzBridge.vst3"

if [[ ! -f "${PLUGIN_FILE}" ]]; then
    echo "ERROR: ${PLUGIN_FILE} not found. Run scripts/build-linux-wine.sh first." >&2
    exit 1
fi

if [[ -n "${1:-}" ]]; then
    VERSION="$1"
else
    VERSION="$(git -C "${REPO_ROOT}" describe --tags --always 2>/dev/null || echo dev)"
fi

PKG_NAME="BuzzBridge-Linux-${VERSION}"
STAGING="${DIST_DIR}/${PKG_NAME}"
TARBALL="${DIST_DIR}/${PKG_NAME}.tar.gz"

echo "[package-linux] version: ${VERSION}"
echo "[package-linux] staging: ${STAGING}"

rm -rf "${STAGING}" "${TARBALL}"
mkdir -p "${STAGING}"

cp "${PLUGIN_FILE}"                        "${STAGING}/BuzzBridge.vst3"
cp "${SCRIPT_DIR}/install-linux.sh"        "${STAGING}/install.sh"
cp "${SCRIPT_DIR}/uninstall-linux.sh"      "${STAGING}/uninstall.sh"
chmod +x "${STAGING}/install.sh" "${STAGING}/uninstall.sh"

cat > "${STAGING}/README.txt" <<EOF
BuzzBridge ${VERSION} — Linux (WINE + yabridge) build
=====================================================

This package contains a 32-bit Windows VST3 plugin that exposes Jeskola
Buzz machine DLLs to any VST3 host. On Linux, it is designed to run via
yabridge (https://github.com/robbert-vdh/yabridge), which bridges Windows
VST3 plugins into native Linux DAWs through WINE.

Requirements:
  - WINE (any recent version)
  - yabridge + yabridgectl

Install:
  ./install.sh

Uninstall:
  ./uninstall.sh

See the project README at https://github.com/nstarke/JeskolaBuzzVST for
more details on pointing BuzzBridge at your Jeskola Buzz machine directory
via the BUZZVST_GEAR_DIR environment variable.
EOF

tar -czf "${TARBALL}" -C "${DIST_DIR}" "${PKG_NAME}"
rm -rf "${STAGING}"

echo "[package-linux] tarball: ${TARBALL}"
