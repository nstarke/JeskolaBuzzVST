#!/usr/bin/env bash
# Linux counterpart of scripts/run_machine_tests.bat.
#
# Builds the BuzzMachineTests target with the llvm-mingw cross toolchain, runs
# it under WINE against a directory of Buzz machine DLLs, and writes the result
# table to machine-support_linux.txt (the Linux equivalent of the Windows
# machine-support_windows.txt). Each machine is loaded, initialized and checked for
# audio output in an isolated child process, so a crashing machine can't take
# down the whole run.
#
# Usage:
#   ./scripts/run_machine_tests_linux.sh [gear_dir]
#
#   gear_dir   Directory of Buzz machine DLLs to test. May be a Linux path
#              (converted to a WINE path automatically) or an already-WINE
#              path (e.g. 'Z:\home\you\Buzz\Gear' or 'C:\...').
#
# Resolution order for the gear directory:
#   1. $1 (positional arg)
#   2. $BUZZVST_GEAR_DIR
#   3. $HOME/Buzz/Gear  — if missing/empty, the bundled machine database in
#      data/mdb_machines.zip is extracted there automatically.
#
# Requires: the same toolchain as build-linux-wine.sh (run that once first, or
# let this script bootstrap it) plus `wine` on PATH.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-wine"
OUT_FILE="${REPO_ROOT}/machine-support_linux.txt"
MDB_ZIP="${REPO_ROOT}/data/mdb_machines.zip"

LLVM_MINGW_RELEASE="llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing dependency: $1" >&2; exit 1; }; }
need wine
need winepath

# --- locate the cross toolchain -------------------------------------------
if [[ -z "${LLVM_MINGW_ROOT:-}" ]]; then
    LLVM_MINGW_ROOT="${REPO_ROOT}/third_party/llvm-mingw/${LLVM_MINGW_RELEASE}"
fi
if [[ ! -x "${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang" ]]; then
    echo "[machine-tests] llvm-mingw toolchain not found at:"
    echo "    ${LLVM_MINGW_ROOT}"
    echo "[machine-tests] running build-linux-wine.sh once to bootstrap it..."
    "${SCRIPT_DIR}/build-linux-wine.sh"
fi
export LLVM_MINGW_ROOT

# --- ensure the build is configured ---------------------------------------
if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "[machine-tests] configuring build-wine..."
    "${SCRIPT_DIR}/build-linux-wine.sh"
fi

# --- build the test target -------------------------------------------------
echo "[machine-tests] building BuzzMachineTests..."
cmake --build "${BUILD_DIR}" --target BuzzMachineTests

EXE="$(find "${BUILD_DIR}" -type f -name 'BuzzMachineTests.exe' | head -n1)"
if [[ -z "${EXE}" ]]; then
    echo "[machine-tests] could not locate BuzzMachineTests.exe" >&2
    exit 1
fi

# --- resolve the gear directory -------------------------------------------
GEAR_ARG="${1:-}"
WINE_GEAR=""

is_wine_path() { [[ "$1" =~ ^[A-Za-z]:\\ ]]; }

if [[ -n "${GEAR_ARG}" ]]; then
    if is_wine_path "${GEAR_ARG}"; then
        WINE_GEAR="${GEAR_ARG}"
    else
        WINE_GEAR="$(winepath -w "${GEAR_ARG}")"
    fi
elif [[ -n "${BUZZVST_GEAR_DIR:-}" ]]; then
    # Convention: BUZZVST_GEAR_DIR is already a WINE path.
    WINE_GEAR="${BUZZVST_GEAR_DIR}"
else
    GEAR_DIR="${HOME}/Buzz/Gear"
    if [[ ! -d "${GEAR_DIR}" ]] || [[ -z "$(ls -A "${GEAR_DIR}" 2>/dev/null)" ]]; then
        if [[ ! -f "${MDB_ZIP}" ]]; then
            echo "[machine-tests] no gear dir and no ${MDB_ZIP} to extract." >&2
            echo "  Pass a gear directory or set BUZZVST_GEAR_DIR." >&2
            exit 1
        fi
        echo "[machine-tests] extracting bundled machine database to ${GEAR_DIR}..."
        mkdir -p "${GEAR_DIR}"
        if command -v unzip >/dev/null 2>&1; then
            unzip -q -o "${MDB_ZIP}" -d "${GEAR_DIR}"
        else
            python3 -m zipfile -e "${MDB_ZIP}" "${GEAR_DIR}"
        fi
    fi
    WINE_GEAR="$(winepath -w "${GEAR_DIR}")"
fi

echo "[machine-tests] gear dir (WINE): ${WINE_GEAR}"

# A hard-crashing machine would otherwise pop WINE's interactive crash-debugger
# dialog and block the unattended sweep. The harness already records crashed
# children by exit code, so disable the dialog (process just dies on a fault).
WINEDEBUG=-all wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog \
    /t REG_DWORD /d 0 /f >/dev/null 2>&1 || true

echo "[machine-tests] running — this can take a while (one child process per machine)..."

# --- run, tee to console, slice the report into the output file -----------
TMP_OUT="$(mktemp)"
trap 'rm -f "${TMP_OUT}"' EXIT

RC=0
WINEDEBUG="${WINEDEBUG:-fixme-all,err-all}" wine "${EXE}" "${WINE_GEAR}" 2>/dev/null \
    | tee "${TMP_OUT}" || RC=$?

# Mirror the .bat: keep everything from the "Found N machines total" line on.
if grep -qE 'Found [0-9]+ machines total' "${TMP_OUT}"; then
    sed -n '/Found [0-9]\+ machines total/,$p' "${TMP_OUT}" > "${OUT_FILE}"
    echo "[machine-tests] wrote ${OUT_FILE}"
else
    echo "[machine-tests] WARNING: no 'Found N machines total' marker in output;" >&2
    echo "                machine-support_linux.txt not written." >&2
fi

exit "${RC}"
