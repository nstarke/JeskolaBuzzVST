#!/usr/bin/env bash
# BuzzBridge Linux uninstaller. Removes BuzzBridge.vst3 from the install
# directory, unregisters it from yabridge, and runs yabridgectl sync to
# clean up the generated .so shim in ~/.vst3/.
#
# Uses the same default install directory as install.sh. Override with
# BUZZVST_INSTALL_DIR if you installed to a custom location.

set -euo pipefail

INSTALL_DIR="${BUZZVST_INSTALL_DIR:-${HOME}/.local/share/BuzzBridge}"
WINEPREFIX_BUZZ="${BUZZVST_WINEPREFIX:-${HOME}/.wine-buzz32}"

RC_BEGIN="# >>> BuzzBridge (yabridge 32-bit runtime) >>>"
RC_END="# <<< BuzzBridge (yabridge 32-bit runtime) <<<"

echo "[BuzzBridge] Uninstalling from: ${INSTALL_DIR}"

# Remove the WINE runtime env block install.sh may have written to shell rc
# files. Check every candidate rc; only our marked block is touched.
for rc in "${BUZZVST_RC_FILE:-}" "${HOME}/.bashrc" "${HOME}/.zshrc" "${HOME}/.profile"; do
    [ -n "${rc}" ] && [ -f "${rc}" ] || continue
    if grep -qF "${RC_BEGIN}" "${rc}"; then
        awk -v b="${RC_BEGIN}" -v e="${RC_END}" '$0==b{s=1} s==0{print} $0==e{s=0}' "${rc}" > "${rc}.bbtmp" && mv "${rc}.bbtmp" "${rc}"
        echo "[BuzzBridge] Removed WINE runtime env block from ${rc}"
    fi
done

# Remove the systemd graphical-session env file install.sh wrote so desktop-
# launched DAWs could find the bridge, and drop the vars from the live session.
SESSION_ENV_FILE="${XDG_CONFIG_HOME:-${HOME}/.config}/environment.d/buzzbridge.conf"
if [[ -f "${SESSION_ENV_FILE}" ]]; then
    rm -f "${SESSION_ENV_FILE}"
    echo "[BuzzBridge] Removed graphical-session env file: ${SESSION_ENV_FILE}"
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl --user unset-environment WINEPREFIX WINELOADER BUZZVST_GEAR_DIR 2>/dev/null || true
fi

if command -v yabridgectl >/dev/null 2>&1; then
    # Remove the top-level convenience symlink created by install.sh before
    # syncing (after sync the shim it points at is gone, leaving it dangling).
    vst3_loc="$(yabridgectl status 2>/dev/null | sed -n "s/^VST3 location: '\(.*\)'\$/\1/p" | head -1)"
    vst3_loc="${vst3_loc:-${HOME}/.vst3/yabridge}"
    toplevel_link="$(dirname "${vst3_loc}")/BuzzBridge.vst3"
    if [[ -L "${toplevel_link}" ]]; then
        rm -f "${toplevel_link}"
        echo "[BuzzBridge] Removed shim symlink: ${toplevel_link}"
    fi

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

# Leave the dedicated win32 prefix in place by default (it is cheap and may hold
# user state); remove it explicitly if requested.
if [[ -d "${WINEPREFIX_BUZZ}" ]]; then
    echo "[BuzzBridge] Left 32-bit WINE prefix in place: ${WINEPREFIX_BUZZ}"
    echo "    Remove it manually with: rm -rf '${WINEPREFIX_BUZZ}'"
fi

echo "[BuzzBridge] Uninstall complete."
