#!/usr/bin/env bash
# BuzzBridge Linux installer.
#
# This script is shipped inside the BuzzBridge-Linux-<version>.tar.gz release
# tarball next to BuzzBridge.vst3. Run it after extracting:
#
#     tar xf BuzzBridge-Linux-v1.0.0.tar.gz
#     cd BuzzBridge-Linux-v1.0.0
#     ./install.sh
#
# What it does:
#   1. Copies BuzzBridge.vst3 into a dedicated directory under your home
#      (default: ~/.local/share/BuzzBridge/). The directory is used as the
#      yabridge "watched" path so the generated .so shim doesn't pollute
#      ~/.vst3/ with unrelated files.
#   2. Registers that directory with yabridgectl and runs `yabridgectl sync`,
#      which produces a native Linux VST3 shim in ~/.vst3/ that your DAW
#      will discover on next rescan.
#
# Override the install directory with:
#   BUZZVST_INSTALL_DIR=/custom/path ./install.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_FILE="${SCRIPT_DIR}/BuzzBridge.vst3"
INSTALL_DIR="${BUZZVST_INSTALL_DIR:-${HOME}/.local/share/BuzzBridge}"

if [[ ! -f "${PLUGIN_FILE}" ]]; then
    echo "ERROR: BuzzBridge.vst3 not found next to install.sh" >&2
    echo "Expected: ${PLUGIN_FILE}" >&2
    exit 1
fi

echo "[BuzzBridge] Installing to: ${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"
cp "${PLUGIN_FILE}" "${INSTALL_DIR}/BuzzBridge.vst3"
echo "[BuzzBridge] Copied BuzzBridge.vst3"

if command -v yabridgectl >/dev/null 2>&1; then
    # `yabridgectl add` is idempotent; ignore "already in list" exit codes.
    yabridgectl add "${INSTALL_DIR}" 2>/dev/null || true
    echo "[BuzzBridge] Running yabridgectl sync..."
    yabridgectl sync
    echo
    echo "[BuzzBridge] Installation complete."
    echo "  Rescan plugins in your DAW to pick up Buzz Generator Bridge and Buzz Effect Bridge."
else
    echo
    echo "WARNING: yabridgectl not found on PATH."
    echo "  BuzzBridge.vst3 has been copied to: ${INSTALL_DIR}"
    echo "  To finish installation, install yabridge from:"
    echo "      https://github.com/robbert-vdh/yabridge"
    echo "  and then run:"
    echo "      yabridgectl add '${INSTALL_DIR}'"
    echo "      yabridgectl sync"
fi

cat <<EOF

Optional environment variables (set before launching your DAW):
  BUZZVST_GEAR_DIR    Path to your Jeskola Buzz Gear directory.
                      Defaults check %ProgramFiles(x86)%\\Jeskola Buzz\\Gear
                      inside your WINE prefix, then %USERPROFILE%\\Buzz\\Gear.
  BUZZVST_BACKUP_DIR  Path for preset backup mirrors. Disabled if unset.

Uninstall with: ./uninstall.sh
EOF
