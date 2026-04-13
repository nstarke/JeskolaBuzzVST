#!/usr/bin/env bash
# BuzzBridge Linux uninstaller. Removes BuzzBridge.vst3 from the install
# directory, unregisters it from yabridge, and runs yabridgectl sync to
# clean up the generated .so shim in ~/.vst3/.
#
# Uses the same default install directory as install.sh. Override with
# BUZZVST_INSTALL_DIR if you installed to a custom location.

set -euo pipefail

INSTALL_DIR="${BUZZVST_INSTALL_DIR:-${HOME}/.local/share/BuzzBridge}"

echo "[BuzzBridge] Uninstalling from: ${INSTALL_DIR}"

if command -v yabridgectl >/dev/null 2>&1; then
    yabridgectl rm "${INSTALL_DIR}" 2>/dev/null || true
    yabridgectl sync
    echo "[BuzzBridge] Unregistered from yabridge."
fi

if [[ -f "${INSTALL_DIR}/BuzzBridge.vst3" ]]; then
    rm -f "${INSTALL_DIR}/BuzzBridge.vst3"
    echo "[BuzzBridge] Removed BuzzBridge.vst3"
fi

# Remove the install directory if it's now empty.
rmdir "${INSTALL_DIR}" 2>/dev/null || true

echo "[BuzzBridge] Uninstall complete."
