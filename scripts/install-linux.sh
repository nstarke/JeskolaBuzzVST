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
#   3. Optionally downloads the Buzz machine database (~30 MB, 726 machines)
#      from the matching GitHub release and extracts it to $HOME/Buzz/Gear.
#      Skipped if --no-machines is passed or the directory already exists.
#
# Options:
#   --with-machines    Download machine DB without prompting.
#   --no-machines      Skip machine DB download without prompting.
#
# Overrides:
#   BUZZVST_INSTALL_DIR   Plugin install directory (default ~/.local/share/BuzzBridge)
#   BUZZVST_GEAR_DIR      Buzz machine directory (default $HOME/Buzz/Gear)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_FILE="${SCRIPT_DIR}/BuzzBridge.vst3"
INSTALL_DIR="${BUZZVST_INSTALL_DIR:-${HOME}/.local/share/BuzzBridge}"
GEAR_DIR="${BUZZVST_GEAR_DIR:-${HOME}/Buzz/Gear}"

# Release asset URL for the machine database. @MDB_URL@ is substituted by
# scripts/package-linux.sh at tarball build time with the version-pinned
# GitHub release asset URL. If left unsubstituted (local/dev tarball), the
# machine DB install step is unavailable.
MDB_URL="@MDB_URL@"

# --- parse args ----------------------------------------------------------
MACHINES_MODE="prompt"  # prompt | install | skip
for arg in "$@"; do
    case "$arg" in
        --with-machines) MACHINES_MODE="install" ;;
        --no-machines)   MACHINES_MODE="skip" ;;
        -h|--help)       sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 1 ;;
    esac
done

# --- step 1: install the plugin -------------------------------------------
if [[ ! -f "${PLUGIN_FILE}" ]]; then
    echo "ERROR: BuzzBridge.vst3 not found next to install.sh" >&2
    echo "Expected: ${PLUGIN_FILE}" >&2
    exit 1
fi

echo "[BuzzBridge] Installing plugin to: ${INSTALL_DIR}"
mkdir -p "${INSTALL_DIR}"
cp "${PLUGIN_FILE}" "${INSTALL_DIR}/BuzzBridge.vst3"
echo "[BuzzBridge] Copied BuzzBridge.vst3"

# --- step 2: register with yabridge ---------------------------------------
if command -v yabridgectl >/dev/null 2>&1; then
    # `yabridgectl add` is idempotent; ignore "already in list" exit codes.
    yabridgectl add "${INSTALL_DIR}" 2>/dev/null || true
    echo "[BuzzBridge] Running yabridgectl sync..."
    yabridgectl sync
    echo "[BuzzBridge] Registered with yabridge."
else
    echo
    echo "WARNING: yabridgectl not found on PATH."
    echo "  BuzzBridge.vst3 has been copied to: ${INSTALL_DIR}"
    echo "  To finish plugin installation, install yabridge from:"
    echo "      https://github.com/robbert-vdh/yabridge"
    echo "  and then run:"
    echo "      yabridgectl add '${INSTALL_DIR}'"
    echo "      yabridgectl sync"
fi

# --- step 3: machine database ---------------------------------------------
# Decide whether to install the machine DB.
install_machines=0
case "${MACHINES_MODE}" in
    install) install_machines=1 ;;
    skip)    install_machines=0 ;;
    prompt)
        if [[ -d "${GEAR_DIR}" ]] && [[ -n "$(ls -A "${GEAR_DIR}" 2>/dev/null)" ]]; then
            echo
            echo "[BuzzBridge] Gear directory already exists and is non-empty:"
            echo "    ${GEAR_DIR}"
            echo "    Skipping machine database download."
            install_machines=0
        else
            echo
            read -r -p "Download Buzz machine database (~30 MB, 726 machines) to ${GEAR_DIR}? [y/N] " reply
            case "${reply}" in [yY]|[yY][eE][sS]) install_machines=1 ;; *) install_machines=0 ;; esac
        fi
        ;;
esac

if [[ "${install_machines}" -eq 1 ]]; then
    if [[ "${MDB_URL}" == "@MDB_URL@" ]] || [[ -z "${MDB_URL}" ]]; then
        echo
        echo "ERROR: This tarball was not built from a tagged release, so the" >&2
        echo "machine database URL is unavailable. Download mdb_machines.zip" >&2
        echo "manually from https://github.com/nstarke/JeskolaBuzzVST/releases" >&2
        echo "and extract it to ${GEAR_DIR}." >&2
    else
        echo
        echo "[BuzzBridge] Downloading machine database..."
        echo "    from: ${MDB_URL}"
        tmpdir="$(mktemp -d)"
        trap 'rm -rf "${tmpdir}"' EXIT
        zip_path="${tmpdir}/mdb_machines.zip"

        if command -v curl >/dev/null 2>&1; then
            curl -fL --progress-bar "${MDB_URL}" -o "${zip_path}"
        elif command -v wget >/dev/null 2>&1; then
            wget -q --show-progress "${MDB_URL}" -O "${zip_path}"
        else
            echo "ERROR: neither curl nor wget found; cannot download." >&2
            exit 1
        fi

        echo "[BuzzBridge] Extracting to ${GEAR_DIR}..."
        mkdir -p "${GEAR_DIR}"

        if command -v unzip >/dev/null 2>&1; then
            unzip -q -o "${zip_path}" -d "${GEAR_DIR}"
        elif command -v python3 >/dev/null 2>&1; then
            python3 -m zipfile -e "${zip_path}" "${GEAR_DIR}"
        else
            echo "ERROR: neither unzip nor python3 found; cannot extract." >&2
            echo "The downloaded zip is at: ${zip_path}" >&2
            trap - EXIT
            exit 1
        fi

        echo "[BuzzBridge] Machine database extracted."
    fi
fi

# --- step 4: BUZZVST_GEAR_DIR guidance ------------------------------------
# BuzzBridge runs inside WINE and resolves paths through the WINE drive
# mapping. To point it at $HOME/Buzz/Gear (outside the WINE prefix), the
# user needs to set BUZZVST_GEAR_DIR to the Z:\... path that WINE maps to
# the Linux root filesystem.
linux_user="${USER:-$(id -un)}"
wine_gear_path="Z:\\home\\${linux_user}\\Buzz\\Gear"

cat <<EOF

Installation complete.

To let BuzzBridge find your machines automatically, export this before
launching your DAW (or add it to ~/.profile / ~/.bashrc):

    export BUZZVST_GEAR_DIR='${wine_gear_path}'

You can also point the plugin at any path using the "Load Buzz Machine..."
browser inside your DAW.

Rescan plugins in your DAW to pick up Buzz Generator Bridge and
Buzz Effect Bridge.

Uninstall with: ./uninstall.sh
EOF
