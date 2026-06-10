#!/usr/bin/env bash
# BuzzBridge Linux installer.
#
# This script is shipped inside the BuzzBridge-Linux-<version>.tar.gz release
# tarball next to BuzzBridge.vst3 as `installer.sh`. Run it after extracting:
#
#     tar xf BuzzBridge-Linux-v1.0.0.tar.gz
#     cd BuzzBridge-Linux-v1.0.0
#     ./installer.sh
#
# What it does:
#   1. Copies BuzzBridge.vst3 into a dedicated directory under your home
#      (default: ~/.local/share/BuzzBridge/). The directory is used as the
#      yabridge "watched" path so the generated .so shim doesn't pollute
#      ~/.vst3/ with unrelated files.
#   2. Registers that directory with yabridgectl and runs `yabridgectl sync`,
#      which produces a native Linux VST3 shim in ~/.vst3/ that your DAW
#      will discover on next rescan.
#   3. Creates a dedicated 32-bit (win32) WINE prefix for the bridge host and
#      writes the WINE runtime env (WINEPREFIX, and WINELOADER if the default
#      wine is 64-bit/WoW64-only) into your shell rc file. Idempotent: an
#      existing BuzzBridge env block is left alone unless its values changed.
#   4. Optionally downloads the Buzz machine database (~30 MB, 726 machines)
#      from the matching GitHub release and extracts it to $HOME/Buzz/Gear.
#      Skipped if --no-machines is passed or the directory already exists.
#
# Options:
#   --deps             Install missing system packages without prompting.
#   --no-deps          Skip the system dependency step entirely.
#   --wine-env         Write WINE runtime env to the shell rc without prompting.
#   --no-wine-env      Skip the win32-prefix / shell-rc step entirely.
#   --with-machines    Download machine DB without prompting.
#   --no-machines      Skip machine DB download without prompting.
#
# Overrides:
#   BUZZVST_INSTALL_DIR   Plugin install directory (default ~/.local/share/BuzzBridge)
#   BUZZVST_GEAR_DIR      Buzz machine directory (default $HOME/Buzz/Gear)
#   BUZZVST_WINEPREFIX    32-bit WINE prefix for the host (default ~/.wine-buzz32)
#   BUZZVST_RC_FILE       Shell rc file to edit (default per $SHELL)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_FILE="${SCRIPT_DIR}/BuzzBridge.vst3"
INSTALL_DIR="${BUZZVST_INSTALL_DIR:-${HOME}/.local/share/BuzzBridge}"
GEAR_DIR="${BUZZVST_GEAR_DIR:-${HOME}/Buzz/Gear}"

# BuzzBridge is a 32-bit plugin, so yabridge's 32-bit host needs a 32-bit (win32)
# WINE prefix; a normal 64-bit ~/.wine prefix cannot be driven by a 32-bit
# wineserver. We keep a dedicated win32 prefix so the user's main ~/.wine is
# untouched. Override with BUZZVST_WINEPREFIX.
WINEPREFIX_BUZZ="${BUZZVST_WINEPREFIX:-${HOME}/.wine-buzz32}"

# Z:\ path that WINE maps to the Linux root, used to point the plugin at the
# native gear directory from inside the prefix.
linux_user="${USER:-$(id -un)}"
wine_gear_path="Z:\\home\\${linux_user}\\Buzz\\Gear"

# Release asset URL for the machine database. @MDB_URL@ is substituted by
# scripts/package-linux.sh at tarball build time with the version-pinned
# GitHub release asset URL. If left unsubstituted (local/dev tarball), the
# machine DB install step is unavailable.
MDB_URL="@MDB_URL@"

# --- parse args ----------------------------------------------------------
MACHINES_MODE="prompt"  # prompt | install | skip
DEPS_MODE="prompt"      # prompt | install | skip
WINE_ENV_MODE="prompt"  # prompt | install | skip
for arg in "$@"; do
    case "$arg" in
        --deps)          DEPS_MODE="install" ;;
        --no-deps)       DEPS_MODE="skip" ;;
        --wine-env)      WINE_ENV_MODE="install" ;;
        --no-wine-env)   WINE_ENV_MODE="skip" ;;
        --with-machines) MACHINES_MODE="install" ;;
        --no-machines)   MACHINES_MODE="skip" ;;
        -h|--help)       sed -n '2,39p' "$0"; exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 1 ;;
    esac
done

# --- step 0: system dependencies -----------------------------------------
# BuzzBridge is a 32-bit Windows VST3 run through WINE + yabridge, so the host
# needs WINE *with 32-bit support* (the default 64-bit/WoW64 wine alone cannot
# launch yabridge's 32-bit host). curl/wget and unzip are needed for the
# optional machine database. yabridge itself is not in most distro repos, so it
# is checked but not auto-installed (see https://github.com/robbert-vdh/yabridge).

detect_pkg_mgr() {
    local m
    for m in apt-get dnf zypper pacman; do
        if command -v "$m" >/dev/null 2>&1; then echo "$m"; return 0; fi
    done
    return 1
}

# True if a 32-bit WINE loader is available (required to run the 32-bit bridge).
have_32bit_wine() {
    [ -x /usr/lib/i386-linux-gnu/wine/wine ] && return 0   # Debian/Ubuntu
    [ -x /usr/lib32/wine/wine ] && return 0                # some multilib layouts
    command -v wine32 >/dev/null 2>&1 && return 0
    ls /usr/lib*/wine/i386-* >/dev/null 2>&1 && return 0   # Fedora/Arch lib dir
    return 1
}

install_system_deps() {
    [[ "${DEPS_MODE}" == "skip" ]] && return 0

    local missing=()
    command -v wine  >/dev/null 2>&1 || missing+=("wine")
    have_32bit_wine                  || missing+=("wine(32-bit support)")
    command -v unzip >/dev/null 2>&1 || missing+=("unzip")
    { command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1; } || missing+=("curl")

    if [[ ${#missing[@]} -eq 0 ]]; then
        echo "[BuzzBridge] System dependencies OK (wine + 32-bit support, unzip, curl/wget)."
        return 0
    fi

    local mgr
    if ! mgr="$(detect_pkg_mgr)"; then
        echo "WARNING: missing dependencies (${missing[*]}) and no supported package" >&2
        echo "  manager (apt/dnf/zypper/pacman) was detected. Install WINE with 32-bit" >&2
        echo "  support, plus curl and unzip, using your distro's tools." >&2
        return 0
    fi

    # Distro-specific commands to install WINE (with 32-bit support) + tools.
    local -a cmds=()
    case "${mgr}" in
        apt-get)
            cmds=(
                "sudo dpkg --add-architecture i386"
                "sudo apt-get update"
                "sudo apt-get install -y wine wine32:i386 wine64 curl unzip"
            ) ;;
        dnf)
            cmds=( "sudo dnf install -y wine wine-core.i686 curl unzip" ) ;;
        zypper)
            cmds=( "sudo zypper install -y wine wine-32bit curl unzip" ) ;;
        pacman)
            echo "[BuzzBridge] NOTE: 32-bit WINE on Arch requires the [multilib] repo"
            echo "    enabled in /etc/pacman.conf (uncomment the [multilib] section)."
            cmds=( "sudo pacman -S --needed --noconfirm wine curl unzip" ) ;;
    esac

    echo
    echo "[BuzzBridge] Missing system dependencies: ${missing[*]}"
    echo "[BuzzBridge] The following commands will install them (${mgr}):"
    local c; for c in "${cmds[@]}"; do echo "    ${c}"; done

    if [[ "${DEPS_MODE}" == "prompt" ]]; then
        echo
        read -r -p "Run these now (requires sudo)? [y/N] " reply
        case "${reply}" in [yY]|[yY][eE][sS]) ;; *) echo "[BuzzBridge] Skipping dependency install."; return 0 ;; esac
    fi

    if ! command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
        echo "ERROR: sudo not found and not running as root; cannot install packages." >&2
        echo "  Run the commands above manually as root." >&2
        return 0
    fi

    for c in "${cmds[@]}"; do
        echo "[BuzzBridge] + ${c}"
        if [[ "$(id -u)" -eq 0 ]]; then c="${c#sudo }"; fi
        if ! eval "${c}"; then
            echo "WARNING: command failed: ${c}" >&2
            echo "  Continuing; install the remaining dependencies manually if needed." >&2
            break
        fi
    done

    if have_32bit_wine; then
        echo "[BuzzBridge] WINE 32-bit support is now available."
    else
        echo "WARNING: 32-bit WINE still not detected after install. The bridge needs it" >&2
        echo "  to run; check your distro's wine 32-bit / multilib packages." >&2
    fi
}

install_system_deps

# --- wine runtime helpers -------------------------------------------------
# Find a 32-bit WINE loader binary (not the wine32 wrapper, which resets
# WINEPREFIX). Needed on systems whose default `wine` is 64-bit/WoW64-only and
# therefore cannot launch yabridge's 32-bit winelib host.
find_32bit_wineloader() {
    local w
    for w in /usr/lib/i386-linux-gnu/wine/wine /usr/lib32/wine/wine /usr/lib/wine/wine; do
        if [ -x "$w" ] && file "$w" 2>/dev/null | grep -q "ELF 32-bit"; then
            echo "$w"; return 0
        fi
    done
    return 1
}

# Locate yabridge's 32-bit host launcher.
find_yabridge_host32() {
    local d cand
    d="$(command -v yabridgectl 2>/dev/null)" && d="$(dirname "$d")" || d=""
    for cand in "${d}/yabridge-host-32.exe" "${HOME}/.local/share/yabridge/yabridge-host-32.exe"; do
        [ -n "$cand" ] && [ -x "$cand" ] && { echo "$cand"; return 0; }
    done
    return 1
}

# Returns 0 if the 32-bit host launches successfully with the given loader
# (may be empty -> default wine) and prefix. The host prints "compatibility
# mode" in its usage banner when it starts.
test_host32() { # $1=loader(may be empty) $2=prefix $3=host
    local out
    # Use literal assignment prefixes; an expanded "VAR=val" is NOT treated as
    # an assignment by the shell (it would be run as a command), so branch.
    if [ -n "$1" ]; then
        out="$(WINEDEBUG=-all WINELOADER="$1" WINEPREFIX="$2" timeout 60 "$3" 2>&1 || true)"
    else
        out="$(WINEDEBUG=-all WINEPREFIX="$2" timeout 60 "$3" 2>&1 || true)"
    fi
    printf '%s' "$out" | grep -q "compatibility mode"
}

detect_rc_file() {
    case "$(basename "${SHELL:-bash}")" in
        zsh)  echo "${ZDOTDIR:-$HOME}/.zshrc" ;;
        bash) echo "${HOME}/.bashrc" ;;
        *)    echo "${HOME}/.profile" ;;
    esac
}

RC_BEGIN="# >>> BuzzBridge (yabridge 32-bit runtime) >>>"
RC_END="# <<< BuzzBridge (yabridge 32-bit runtime) <<<"

# Write (or refresh) the WINE runtime env block in the shell rc file. Idempotent:
# if our marked block already exists with identical contents it is left alone;
# if it differs it is replaced; pre-existing WINEPREFIX/WINELOADER exports set
# outside our block are detected and the user is warned rather than duplicated.
write_wine_env() { # $1=prefix $2=loader(maybe empty)
    local rc; rc="${BUZZVST_RC_FILE:-$(detect_rc_file)}"

    # Build the desired block.
    local block
    block="${RC_BEGIN}
# Added by BuzzBridge installer.sh so yabridge can run the 32-bit BuzzBridge host.
# WARNING: WINEPREFIX/WINELOADER below are GLOBAL for shells that source this
# file and will affect every other Windows app you run under wine. Remove this
# block (or run uninstall.sh) if that is a problem, and launch your DAW with
# these vars set some other way instead.
export WINEPREFIX=\"$1\""
    [ -n "$2" ] && block="${block}
export WINELOADER=\"$2\""
    block="${block}
export BUZZVST_GEAR_DIR='${wine_gear_path}'
${RC_END}"

    touch "$rc" 2>/dev/null || { echo "WARNING: cannot write ${rc}; set the env vars manually (see README)." >&2; return 0; }

    # Already present and unchanged? Then there is nothing to do.
    if grep -qF "${RC_BEGIN}" "$rc"; then
        local existing
        existing="$(awk -v b="${RC_BEGIN}" -v e="${RC_END}" '$0==b{f=1} f{print} $0==e{f=0}' "$rc")"
        if [ "${existing}" = "${block}" ]; then
            echo "[BuzzBridge] WINE runtime env already present and up to date in ${rc}; leaving it."
            return 0
        fi
        echo "[BuzzBridge] Updating existing BuzzBridge env block in ${rc}."
        awk -v b="${RC_BEGIN}" -v e="${RC_END}" '$0==b{s=1} s==0{print} $0==e{s=0}' "$rc" > "${rc}.bbtmp" && mv "${rc}.bbtmp" "$rc"
    else
        # No block of ours yet. If the user already exports these vars elsewhere,
        # don't stomp on them — warn and skip.
        if grep -Eq '^[[:space:]]*export[[:space:]]+(WINEPREFIX|WINELOADER)=' "$rc"; then
            echo "WARNING: ${rc} already sets WINEPREFIX/WINELOADER outside the BuzzBridge block." >&2
            echo "  Not modifying it to avoid conflicts. Ensure these are set for the bridge:" >&2
            echo "      WINEPREFIX=\"$1\"" >&2
            [ -n "$2" ] && echo "      WINELOADER=\"$2\"" >&2
            return 0
        fi
    fi

    printf '\n%s\n' "${block}" >> "$rc"
    echo "[BuzzBridge] Wrote WINE runtime env to ${rc}."
    echo "    Start a new shell (or 'source ${rc}') before launching your DAW from a terminal."
    WROTE_RC="$rc"
}

# Create the win32 prefix (if needed) and configure the shell rc with the
# minimal env required to run the 32-bit bridge host.
configure_wine_runtime() {
    [[ "${WINE_ENV_MODE}" == "skip" ]] && return 0
    command -v wine >/dev/null 2>&1 || return 0   # no wine yet; deps step warned

    local host loader prefix
    host="$(find_yabridge_host32)" || { echo "[BuzzBridge] (wine runtime) yabridge 32-bit host not found; skipping env setup."; return 0; }
    prefix="${WINEPREFIX_BUZZ}"
    loader="$(find_32bit_wineloader || true)"

    # Ensure a win32 prefix exists.
    if [[ ! -f "${prefix}/system.reg" ]]; then
        echo "[BuzzBridge] Creating 32-bit WINE prefix at ${prefix} (one-time, ~30-60s) ..."
        if [[ -n "${loader}" ]]; then
            WINEDEBUG=-all WINEARCH=win32 WINEPREFIX="${prefix}" WINELOADER="${loader}" timeout 180 "${loader}" wineboot --init >/dev/null 2>&1 || true
        else
            WINEDEBUG=-all WINEARCH=win32 WINEPREFIX="${prefix}" timeout 180 wine wineboot --init >/dev/null 2>&1 || true
        fi
    fi

    # Determine the minimal env: always WINEPREFIX; add WINELOADER only if the
    # default wine cannot launch the 32-bit host (keeps other wine apps working
    # where possible).
    local need_loader=""
    if test_host32 "" "${prefix}" "${host}"; then
        need_loader=""
        echo "[BuzzBridge] Default WINE can run the 32-bit host with the win32 prefix."
    elif [[ -n "${loader}" ]] && test_host32 "${loader}" "${prefix}" "${host}"; then
        need_loader="${loader}"
        echo "[BuzzBridge] Using 32-bit WINE loader: ${loader}"
    else
        echo "WARNING: could not get yabridge's 32-bit host to launch automatically." >&2
        echo "  Writing WINEPREFIX anyway; you may need a WINE build with 32-bit support." >&2
    fi

    if [[ "${WINE_ENV_MODE}" == "prompt" ]]; then
        echo
        echo "[BuzzBridge] The 32-bit bridge needs these env vars (GLOBAL wine effect):"
        echo "    WINEPREFIX=${prefix}"
        [[ -n "${need_loader}" ]] && echo "    WINELOADER=${need_loader}"
        read -r -p "Add them to your shell rc file? [y/N] " reply
        case "${reply}" in
            [yY]|[yY][eE][sS]) ;;
            *) echo "[BuzzBridge] Not modifying any rc file. See README for manual setup."; return 0 ;;
        esac
    fi

    write_wine_env "${prefix}" "${need_loader}"
}

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

    # yabridge writes the generated VST3 shim into a 'yabridge/' subdirectory
    # of the VST3 search root (e.g. ~/.vst3/yabridge/BuzzBridge.vst3). Fully
    # VST3-compliant hosts scan that root recursively and find it there, but
    # some hosts -- notably Renoise -- only scan .vst3 bundles sitting directly
    # in the root and never descend into 'yabridge/'. Symlink the shim up one
    # level into the search root so those hosts discover it too.
    vst3_loc="$(yabridgectl status 2>/dev/null | sed -n "s/^VST3 location: '\(.*\)'\$/\1/p" | head -1)"
    vst3_loc="${vst3_loc:-${HOME}/.vst3/yabridge}"
    shim_bundle="${vst3_loc}/BuzzBridge.vst3"
    toplevel_link="$(dirname "${vst3_loc}")/BuzzBridge.vst3"
    if [[ -e "${shim_bundle}" && "${shim_bundle}" != "${toplevel_link}" ]]; then
        ln -sfn "${shim_bundle}" "${toplevel_link}"
        echo "[BuzzBridge] Linked shim into $(dirname "${toplevel_link}")/ for non-recursive hosts (e.g. Renoise)."
    fi
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

# --- step 2.5: wine runtime (win32 prefix + shell rc env) -----------------
WROTE_RC=""
configure_wine_runtime

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

# --- step 4: final guidance -----------------------------------------------
echo
echo "Installation complete."
echo
if [[ -n "${WROTE_RC}" ]]; then
    cat <<EOF
WINE runtime env (WINEPREFIX, WINELOADER if needed, BUZZVST_GEAR_DIR) was added
to: ${WROTE_RC}
Start a new terminal (or 'source ${WROTE_RC}') before launching your DAW from a
shell so the bridge can start. Note: those WINE vars are global — see the README
if you also run other Windows apps under wine.
EOF
else
    cat <<EOF
To run the 32-bit bridge, these need to be set before launching your DAW
(add them to ~/.bashrc / ~/.zshrc / ~/.profile, or use a launcher wrapper):

    export WINEPREFIX="${WINEPREFIX_BUZZ}"
    # export WINELOADER=/usr/lib/i386-linux-gnu/wine/wine   # only on 64-bit/WoW64-only wine
    export BUZZVST_GEAR_DIR='${wine_gear_path}'
EOF
fi
cat <<EOF

You can also point the plugin at any machine path using the "Load Buzz
Machine..." browser inside your DAW.

Rescan plugins in your DAW to pick up Buzz Generator Bridge and
Buzz Effect Bridge.

Uninstall with: ./uninstall.sh
EOF
